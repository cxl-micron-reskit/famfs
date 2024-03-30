#!/usr/bin/bash

TEST_FUNCS=scripts/test_funcs.sh
if [ ! -f $TEST_FUNCS ]; then
	echo "Can't source $TEST_FUNCS"
	exit -1
fi
BIN=debug
if [ ! -d $BIN ]; then
	echo "Can't find executables"
	exit -1
fi

cwd=$(pwd)
export PATH=cwd/debug:$PATH

DEVTYPE="$1"
echo "DEVTYPE=$DEVTYPE"

MPT="/mnt/famfs"
#OWNER="jgroves.jgroves"
CLI="sudo debug/famfs"

source scripts/test_funcs.sh || fail "can't source test_funcs.sh"

test -f $MPT || test -D $MPT && fail "mount point $MPT is not a directory"

set -x
daxctl list || fail "need daxctl"
ndctl list  || fail "need ndctl"

#sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
sudo mkdir -p $MPT        || fail "mkdir"
#sudo chown $OWNER $MPT
sudo modprobe famfs  || fail "modprobe"

verify_not_mounted $DEV $MPT "Already mounted"
full_mount $DEV $MPT "setup: mount"
verify_mounted $DEV $MPT "mount failed"

set +x
echo "*************************************************************************************"
echo " Setup completed successfully"
echo "*************************************************************************************"
