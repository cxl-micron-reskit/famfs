#!/usr/bin/bash

fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

MOUNT_PT="/mnt/xfs"
OWNER="jgroves.jgroves"

if [ ! -d $MOUNT_PT ]; then
    echo "Error MOUNT_PT ($MOUNT_PT) is not a directory"
    exit -1
fi

set -x
daxctl list || fail "need daxctl"
ndctl list  || fail "need ndctl"

#sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
sudo mkdir -p $MOUNT_PT || fail "mkdir"
#chown $OWNER $MOUNT_PT

sudo mkfs.xfs  -m reflink=0 -d daxinherit=1 /dev/pmem0       || fail "mkfs"
sudo mount -t xfs -o noatime,dax=always /dev/pmem0 $MOUNT_PT || fail "mount"
df -hT | grep $MOUNT_PT

sudo dd if=/dev/urandom bs=1048576 count=4 of=$MOUNT_PT/frog1 || fail "dd"
