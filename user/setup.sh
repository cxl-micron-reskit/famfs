#!/usr/bin/bash

cwd=$(pwd)
export PATH=cwd/debug:$PATH


MPT="/mnt/famfs"
#OWNER="jgroves.jgroves"
CLI="sudo debug/famfs"

source test_funcs.sh

test -f $MPT || test -D $MPT && fail "mount point $MPT is not a directory"

set -x
daxctl list || fail "need daxctl"
ndctl list  || fail "need ndctl"

#sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
sudo mkdir -p $MPT        || fail "mkdir"
#sudo chown $OWNER $MPT
sudo insmod ../kmod/famfs.ko  || fail "insmod"

verify_not_mounted $DEV $MPT "Already mounted"
full_mount $DEV $MPT "setup: mount"
#sudo mount -t famfs -o noatime -o dax=always /dev/pmem0 $MPT || fail "mount"
verify_mounted $DEV $MPT "mount failed"

set +x
echo "*************************************************************************************"
echo " Setup completed successfully"
echo "*************************************************************************************"
