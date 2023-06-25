#!/usr/bin/bash

MOUNT_PT=/mnt/tagfs

sudo umount $MOUNT_PT
sudo rmmod tagfs
