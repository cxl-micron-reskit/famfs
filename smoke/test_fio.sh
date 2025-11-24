#!/usr/bin/env bash

source smoke/test_header.sh

TEST="test_fio"

source $SCRIPTS/test_funcs.sh

echo "SCRIPTS=$SCRIPTS $(realpath $SCRIPTS)"
REALSCRIPTS=$(realpath $SCRIPTS)

set -x

# Start with a clean, empty file systeem
famfs_recreate "test_fio"

verify_mounted $DEV $MPT "test_fio.sh"

TESTDIR="$MPT/fio_stress"
ecpect_good ${CLI} mkdir -p $TESTDIR -- "mkdir $TESTDIR should succeed"

SPACE_AVAIL=$(sudo $BIN/famfs ${FSCK} $TESTDIR | grep "Free space" | awk -e '{print $3}')

pwd
# Not a stress test, just a smoke test (4 jobs)
expect_good $REALSCRIPTS/stress_fio.sh \
		       -v \
		       -b $BIN \
		       -r 30 \
		       -s $SPACE_AVAIL \
		       -p $TESTDIR \
		       -j 4 -- "test_fio failed"

SHADOW=/tmp/smoke.shadow/test_fio.shadow/root
expect_good sudo mkdir -p $SHADOW -- "mkdir $SHADOW should work"
expect_good ${CLI} logplay -sS $SHADOW $MPT -- "shadow logplay should work"

set +x
echo ":==*************************************************************************"
echo ":==$TEST completed successfully"
echo ":==*************************************************************************"
exit 0
