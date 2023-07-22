#!/usr/bin/bash

fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

MOUNT_PT=/mnt/tagfs

if [ ! -d $MOUNT_PT ]; then
    echo "Error MOUNT_PT ($MOUNT_PT) is not a directory"
    exit -1
fi

sudo umount $MOUNT_PT || fail "umount"
sudo rmmod tagfs      || fail "rmmod"
