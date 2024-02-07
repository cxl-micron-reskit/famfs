#!/usr/bin/env bash

#
# This script installs the out-of-tree famfs.ko kernel module,
# but only if the running kernel # does not have an in-tree famfs.ko
#
#set -x
kpath=/lib/modules/$(uname -r)
instpath=$kpath/extra
inst_famfs=$instpath/famfs.ko
famfs=../kmod/famfs.ko
kernel_famfs=$kpath/kernel/fs/famfs/famfs.ko

if [ ! -d $kpath ]; then
    echo "Error: kernel path ($kpath) does not exist"
    exit -1
fi

if (( $# > 0 )); then
    if [[ "$1" == "--replace" ]]; then
	# remove famfs from its kernel build location and install the local version
	sudo rm $kernel_famfs
    fi
fi

if [ -f $kernel_famfs ]; then
    echo "famfs is part of the installed kernel"
    echo "aborting out-of-tree installation"

    # If kernel-native famfs is installed, remove out-of-tree famfs if any
    if [ -f $inst_famfs ]; then
	echo "removing out-of-tree famfs module $inst_famfs"
	sudo rm $inst_famfs
    fi
    sudo depmod -a
    sleep 2
    exit -1
fi

echo "Installing out-of tree famfs kmod"
echo
echo
sudo mkdir -p $instpath
sudo cp $famfs $instpath
sudo depmod -a
sleep 2
