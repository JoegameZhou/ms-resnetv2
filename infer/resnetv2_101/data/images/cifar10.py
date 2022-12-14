#!/usr/bin/env python
# coding=utf-8

""" cifar10.py """

import os
import cv2
import numpy as np

loc_1 = './train_cifar10/'
loc_2 = './test_cifar10/'


def unpickle(file_name):
    import pickle
    with open(file_name, 'rb') as fo:
        dict_res = pickle.load(fo, encoding='bytes')
    return dict_res


def convert_train_data(file_dir):
    """ ./train_cifar10/ """
    if not os.path.exists(loc_1):
        os.mkdir(loc_1)
    for i in range(1, 6):
        data_name = os.path.join(file_dir, 'data_batch_' + str(i))
        data_dict = unpickle(data_name)
        print('{} is processing'.format(data_name))
        for j in range(10000):
            img = np.reshape(data_dict[b'data'][j], (3, 32, 32))
            img = np.transpose(img, (1, 2, 0))
            img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            img_name = "%s%s%s.jpg" % (loc_1, str(data_dict[b'labels'][j]), str((i) * 10000 + j))
            cv2.imwrite(img_name, img)
        print('{} is done'.format(data_name))


def convert_test_data(file_dir):
    """ ./test_cifar10/ && test_label.txt """
    if not os.path.exists(loc_2):
        os.mkdir(loc_2)
    test_data_name = file_dir + '/test_batch'
    print('{} is processing'.format(test_data_name))
    test_dict = unpickle(test_data_name)
    for m in range(10000):
        img = np.reshape(test_dict[b'data'][m], (3, 32, 32))
        img = np.transpose(img, (1, 2, 0))
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img_name = '%s%s%s%s' % (loc_2, str(test_dict[b'labels'][m]), str(10000 + m), '.png')
        img_label = "%s%s.jpg" % (str(test_dict[b'labels'][m]), str(10000 + m))
        cv2.imwrite(img_name, img)
        with open("test_label.txt", "a") as f:
            f.write(img_label + "\t" + str(test_dict[b'labels'][m]))
            f.write("\n")
    print("{} is done".format(test_data_name))


def cifar10_img():
    """
    from    ./cifar-10-batches-py
    to      ./train_cifar10/ ./test_cifar10/ ./test_label.txt
    """
    file_dir = './cifar-10-batches-py'
    convert_train_data(file_dir)
    convert_test_data(file_dir)
    print('Finish transforming to image')


if __name__ == '__main__':
    cifar10_img()
