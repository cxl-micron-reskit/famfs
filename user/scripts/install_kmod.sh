#!/usr/bin/env bash

#
# This script installs the out-of-tree famfs.ko kernel module,
# but only if the running kernel # does not have an in-tree famfs.ko
#

kpath=/lib/modules/$(uname -r)
instpath=$kpath/extra
famfs=../kmod/famfs.ko

if [ ! -d $kpath ]; then
    echo "Error: kernel path ($kpath) does not exist"
    exit -1
fi

if [ -f "$kpath/fs/famfs/famfs.ko" ]; then
    echo "famfs is part of the installed kernel"
    echo "aborting out-of-tree installation"
    exit -1
fi

sudo mkdir -p $instpath
sudo cp $famfs $instpath
sudo depmod -a
