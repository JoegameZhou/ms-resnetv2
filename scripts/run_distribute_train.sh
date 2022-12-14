#!/bin/bash
if [ $# != 4 ]
then
  echo "Usage: bash run_distribute_train.sh [resnetv2_50|resnetv2_101|resnetv2_152] [cifar10|cifar100] [RANK_TABLE_FILE] [DATASET_PATH]"
  exit 1
fi

if [ $1 != "resnetv2_50" ] && [ $1 != "resnetv2_101" ] && [ $1 != "resnetv2_152" ]
then 
  echo "error: the selected net is neither resnetv2_50 nor resnetv2_101 and resnetv2_152"
  exit 1
fi

if [ $2 != "cifar10" ] && [ $2 != "cifar100" ]
then 
    echo "error: the selected dataset is neither cifar10 nor cifar100"
    exit 1
fi

if [ ! -f $3 ]
then 
    echo "error: RANK_TABLE_FILE=$3 is not a file"
    exit 1
fi

if [ ! -d $4 ]
then
    echo "error: DATASET_PATH=$4 is not a directory"
    exit 1
fi

ulimit -u unlimited
export RANK_TABLE_FILE=$3
export DEVICE_NUM=8         
export RANK_SIZE=8          

for((i=0; i<${RANK_SIZE}; i++))
do
    export DEVICE_ID=${i}
    export RANK_ID=${i}
    echo "start distributed training for rank $RANK_ID, device $DEVICE_ID"
    python train.py --net $1 --dataset $2 --run_distribute=True --device_num=$DEVICE_NUM --dataset_path $4 &> log.$i & 
done