#!/usr/bin/bash

MOUNT_PT="/mnt/tagfs"
OWNER="jgroves.jgroves"
set -x
daxctl list
ndctl list

#sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
sudo mkdir -p $MOUNT_PT
#chown $OWNER $MOUNT_PT
sudo insmod ../tagfs/tagfs.ko
sudo mount -t tagfs -o noatime,dax=always /dev/dax0.0 $MOUNT_PT
grep tagfs /proc/mounts
