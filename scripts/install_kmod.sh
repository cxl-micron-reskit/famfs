#!/usr/bin/env bash

#
# This script installs the out-of-tree famfs.ko kernel module,
# but only if the running kernel # does not have an in-tree famfs.ko
#
#set -x
kpath=/lib/modules/$(uname -r)
instpath=$kpath/extra
inst_famfs=$instpath/famfs.ko

# If there is an out of tree build of the kmod, it must be at ./famfs-kmod
famfs=famfs-kmod/famfs.ko 
kernel_famfs=$kpath/kernel/fs/famfs/famfs.ko

if [ ! -d $kpath ]; then
    echo "Error: kernel path ($kpath) does not exist"
    exit -1
fi

echo "***********************************************************************"
if (( $# > 0 )); then
    if [[ "$1" == "--replace" ]]; then
	# remove famfs from its kernel build location and install the local version
	echo "--replace option specified"
	if [ ! -f $famfs ]; then
	    echo "but out of tree module $famfs not found. Bailing..."
	else
	    echo "Removing in-tree famfs.ko ($kernel_famfs) from kernel $(uname -r)"
	    sudo rm $kernel_famfs
	fi
    fi
fi

if [ -f $kernel_famfs ]; then
    echo "famfs is part of the installed kernel"

    # If kernel-native famfs is installed, remove out-of-tree famfs if any
    if [ -f $inst_famfs ]; then
	echo "removing out-of-tree famfs module $inst_famfs"
	sudo rm $inst_famfs
    fi
    echo "Using in-tree famfs.ko from kernel $(uname -r)"
    echo "***********************************************************************"
    echo
    sudo depmod -a
    sleep 2
    exit -1
fi

echo "Installing out-of tree famfs kmod $famfs"
echo "***********************************************************************"
echo
sudo mkdir -p $instpath
sudo cp $famfs $instpath
sudo depmod -a
sleep 2
