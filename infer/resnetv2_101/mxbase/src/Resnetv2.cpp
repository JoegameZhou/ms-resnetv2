#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Resnetv2.h"
#include "MxBase/DeviceManager/DeviceManager.h"
#include "MxBase/Log/Log.h"

namespace {
    const uint32_t YUV_BYTE_NU = 3;
    const uint32_t YUV_BYTE_DE = 2;
    const uint32_t VPC_H_ALIGN = 2;
    const uint32_t MAX_LENGTH = 128;
}

APP_ERROR Resnetv2::Init(const InitParam &initParam) {
    deviceId_ = initParam.deviceId;
    APP_ERROR ret = MxBase::DeviceManager::GetInstance()->InitDevices();
    if (ret != APP_ERR_OK) {
        LogError << "Init devices failed, ret = " << ret << ".";
        return ret;
    }
    ret = MxBase::TensorContext::GetInstance()->SetContext(initParam.deviceId);
    if (ret != APP_ERR_OK) {
        LogError << "Set context failed, ret = " << ret << ".";
        return ret;
    }
    model_ = std::make_shared<MxBase::ModelInferenceProcessor>();
    ret = model_->Init(initParam.modelPath, modelDesc_);
    if (ret != APP_ERR_OK) {
        LogError << "ModelInferenceProcessor init failed, ret = " << ret << ".";
        return ret;
    }
    MxBase::ConfigData configData;
    const std::string softmax = initParam.softmax ? "true" : "false";
    const std::string checkTensor = initParam.checkTensor ? "true" : "false";

    configData.SetJsonValue("CLASS_NUM", std::to_string(initParam.classNum));
    configData.SetJsonValue("TOP_K", std::to_string(initParam.topk));
    configData.SetJsonValue("SOFTMAX", softmax);
    configData.SetJsonValue("CHECK_MODEL", checkTensor);

    auto jsonStr = configData.GetCfgJson().serialize();
    std::map<std::string, std::shared_ptr<void>> config;
    config["postProcessConfigContent"] = std::make_shared<std::string>(jsonStr);
    config["labelPath"] = std::make_shared<std::string>(initParam.labelPath);

    post_ = std::make_shared<MxBase::Resnet50PostProcess>();
    ret = post_->Init(config);
    if (ret != APP_ERR_OK) {
        LogError << "Resnetv2PostProcess init failed, ret = " << ret << ".";
        return ret;
    }

    return APP_ERR_OK;
}

APP_ERROR Resnetv2::DeInit() {
    model_->DeInit();
    post_->DeInit();
    MxBase::DeviceManager::GetInstance()->DestroyDevices();
    return APP_ERR_OK;
}

APP_ERROR Resnetv2::ReadImage(const std::string &imgPath, cv::Mat *imageMat) {
    *imageMat = cv::imread(imgPath, cv::IMREAD_COLOR);
    LogInfo << "image size: " << imageMat->size();
    return APP_ERR_OK;
}

APP_ERROR Resnetv2::ResizeImage(cv::Mat *imageMat) {
    static constexpr uint32_t resizeHeight = 32;
    static constexpr uint32_t resizeWidth = 32;
    cv::resize(*imageMat, *imageMat, cv::Size(resizeWidth, resizeHeight));
    return APP_ERR_OK;
}

APP_ERROR Resnetv2::CVMatToTensorBase(const cv::Mat &imageMat, MxBase::TensorBase *tensorBase) {
    uint32_t batchSize = modelDesc_.inputTensors[0].tensorDims[0];
    const uint32_t dataSize =  imageMat.cols *  imageMat.rows * MxBase::YUV444_RGB_WIDTH_NU * batchSize;
    LogInfo << "image size after crop: [" << imageMat.cols << " x " << imageMat.rows << "]";
    MxBase::MemoryData memoryDataDst(dataSize, MxBase::MemoryData::MEMORY_DEVICE, deviceId_);
    MxBase::MemoryData memoryDataSrc(imageMat.data, dataSize, MxBase::MemoryData::MEMORY_HOST_MALLOC);

    APP_ERROR ret = MxBase::MemoryHelper::MxbsMallocAndCopy(memoryDataDst, memoryDataSrc);
    if (ret != APP_ERR_OK) {
        LogError << GetError(ret) << "Memory malloc failed.";
        return ret;
    }
    std::vector<uint32_t> shape = {imageMat.rows * MxBase::YUV444_RGB_WIDTH_NU * batchSize,
        static_cast<uint32_t>(imageMat.cols)};
    *tensorBase = MxBase::TensorBase(memoryDataDst, false, shape, MxBase::TENSOR_DTYPE_UINT8);
    return APP_ERR_OK;
}

APP_ERROR Resnetv2::Inference(const std::vector<MxBase::TensorBase> &inputs,
                                      std::vector<MxBase::TensorBase> *outputs) {
    auto dtypes = model_->GetOutputDataType();
    for (size_t i = 0; i < modelDesc_.outputTensors.size(); ++i) {
        std::vector<uint32_t> shape = {};
        for (size_t j = 0; j < modelDesc_.outputTensors[i].tensorDims.size(); ++j) {
            shape.push_back((uint32_t)modelDesc_.outputTensors[i].tensorDims[j]);
        }
        MxBase::TensorBase tensor(shape, dtypes[i],  MxBase::MemoryData::MemoryType::MEMORY_DEVICE, deviceId_);
        APP_ERROR ret = MxBase::TensorBase::TensorBaseMalloc(tensor);
        if (ret != APP_ERR_OK) {
            LogError << "TensorBaseMalloc failed, ret = " << ret << ".";
            return ret;
        }
        outputs->push_back(tensor);
    }
    MxBase::DynamicInfo dynamicInfo = {};
    dynamicInfo.dynamicType = MxBase::DynamicType::STATIC_BATCH;
    auto startTime = std::chrono::high_resolution_clock::now();
    APP_ERROR ret = model_->ModelInference(inputs, *outputs, dynamicInfo);
    auto endTime = std::chrono::high_resolution_clock::now();
    double costMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    inferCostTimeMilliSec += costMs;
    if (ret != APP_ERR_OK) {
        LogError << "ModelInference failed, ret = " << ret << ".";
        return ret;
    }
    return APP_ERR_OK;
}

APP_ERROR Resnetv2::PostProcess(const std::vector<MxBase::TensorBase> &inputs,
                                        std::vector<std::vector<MxBase::ClassInfo>> *clsInfos) {
    APP_ERROR ret = post_->Process(inputs, *clsInfos);
    if (ret != APP_ERR_OK) {
        LogError << "Process failed, ret = " << ret << ".";
        return ret;
    }
    return APP_ERR_OK;
}

APP_ERROR Resnetv2::SaveResult(const std::string &imgPath, const std::string &resPath,
                                        const std::vector<std::vector<MxBase::ClassInfo>> &batchClsInfos) {
    LogInfo << "image path: " << imgPath;
    std::string fileName = imgPath.substr(imgPath.find_last_of("/") + 1);
    size_t dot = fileName.find_last_of(".");
    std::string resFileName = resPath + "/" + fileName.substr(0, dot) + "_1.txt";
    LogInfo << "file path for saving result: " << resFileName;
    std::ofstream outfile(resFileName);
    if (outfile.fail()) {
        LogError << "Failed to open result file: ";
        return APP_ERR_COMM_FAILURE;
    }
    uint32_t batchIndex = 0;
    for (auto clsInfos : batchClsInfos) {
        std::string resultStr;
        for (auto clsInfo : clsInfos) {
            LogDebug << "className:" << clsInfo.className << " confidence:" << clsInfo.confidence <<
            " classIndex:" <<  clsInfo.classId;
            resultStr += std::to_string(clsInfo.classId) + " ";
        }
        outfile << resultStr << std::endl;
        batchIndex++;
    }
    outfile.close();
    return APP_ERR_OK;
}

APP_ERROR Resnetv2::Process(const std::string &imgPath, const std::string &resPath) {
    cv::Mat imageMat;
    APP_ERROR ret = ReadImage(imgPath, &imageMat);
    if (ret != APP_ERR_OK) {
        LogError << "ReadImage failed, ret = " << ret << ".";
        return ret;
    }
    ret = ResizeImage(&imageMat);
    if (ret != APP_ERR_OK) {
        LogError << "ResizeImage failed, ret = " << ret << ".";
        return ret;
    }
    std::vector<MxBase::TensorBase> inputs = {};
    std::vector<MxBase::TensorBase> outputs = {};
    MxBase::TensorBase tensorBase;
    ret = CVMatToTensorBase(imageMat, &tensorBase);
    if (ret != APP_ERR_OK) {
        LogError << "CVMatToTensorBase failed, ret = " << ret << ".";
        return ret;
    }
    inputs.push_back(tensorBase);
    auto startTime = std::chrono::high_resolution_clock::now();
    ret = Inference(inputs, &outputs);
    auto endTime = std::chrono::high_resolution_clock::now();
    double costMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    inferCostTimeMilliSec += costMs;
    if (ret != APP_ERR_OK) {
        LogError << "Inference failed, ret = " << ret << ".";
        return ret;
    }
    std::vector<std::vector<MxBase::ClassInfo>> BatchClsInfos = {};
    ret = PostProcess(outputs, &BatchClsInfos);
    if (ret != APP_ERR_OK) {
        LogError << "PostProcess failed, ret = " << ret << ".";
        return ret;
    }
    ret = SaveResult(imgPath, resPath, BatchClsInfos);
    if (ret != APP_ERR_OK) {
        LogError << "Save infer results into file failed. ret = " << ret << ".";
        return ret;
    }
    return APP_ERR_OK;
}
