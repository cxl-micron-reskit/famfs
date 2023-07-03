#!/usr/bin/bash

fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

MOUNT_PT="/mnt/tagfs"
OWNER="jgroves.jgroves"

if [ ! -d $MOUNT_PT ]; then
    echo "Error MOUNT_PT ($MOUNT_PT) is not a directory"
    exit -1
fi

set -x
daxctl list || fail "need daxctl"
ndctl list  || fail " need ndctl"

#sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
sudo mkdir -p $MOUNT_PT        || fail "mkdir"
#sudo chown $OWNER $MOUNT_PT
sudo insmod ../tagfs/tagfs.ko  || fail "insmod"
sudo mount -t tagfs -o noatime -o dax=always /dev/dax0.0 $MOUNT_PT || fail "mount"
grep tagfs /proc/mounts        || fail "/proc/mounts"
