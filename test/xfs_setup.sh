#!/usr/bin/bash

MOUNT_PT="/mnt/xfs"
OWNER="jgroves.jgroves"
set -x
daxctl list
ndctl list

#sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
sudo mkdir -p $MOUNT_PT
#chown $OWNER $MOUNT_PT

sudo mkfs.xfs  -m reflink=0 -d daxinherit=1 /dev/pmem0
sudo mount -t xfs -o noatime,dax=always /dev/pmem0 $MOUNT_PT
df -hT | grep $MOUNT_PT

sudo dd if=/dev/urandom bs=1048576 count=4 of=$MOUNT_PT/frog1
