#!/usr/bin/env bash

source smoke/test_header.sh

TEST="test4"

source $SCRIPTS/test_funcs.sh

MULTICHASE="sudo $BIN/src/multichase/multichase"

# set -x

# Start with a clean, empty file systeem
famfs_recreate "test4"

verify_mounted $DEV $MPT "test4.sh"

expect_fail ${CLI} badarg                            -- "create badarg should fail"
expect_good ${CLI} creat  -h                         -- "creat -h should succeed"
expect_good ${CLI} creat -s 3g  ${MPT}/memfile       -- "can't create memfile for multichase"
expect_good ${CLI} creat -s 100m ${MPT}/memfile1     -- "creat should succeed with -s 100m"
expect_good ${CLI} creat -s 10000k ${MPT}/memfile2   -- "creat with -s 10000k should succeed"

expect_good ${MULTICHASE} -d ${MPT}/memfile -m 2900m -- "multichase fail"

verify_mounted $DEV $MPT "test4.sh mounted"
expect_good sudo $UMOUNT $MPT -- "test4.sh umount"
verify_not_mounted $DEV $MPT "test4.sh"

# Test shadow logplay while the fs is not mounted
SHADOWPATH=/tmp/shadowpath/root
expect_fail ${CLI} logplay --shadow -d /dev/bogodax -- "shadow logplay should fail with bogus daxdev"
sudo rm -rf $SHADOWPATH
expect_fail ${CLI} logplay --shadow $SHADOWPATH/frob --daxdev $DEV -vv -- \
    "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"
expect_fail ${CLI} logplay --daxdev $DEV -vv  $SHADOWPATH -- \
    "logplay should fail if --daxdev is set without --shadow"

sudo rm -rf $SHADOWPATH
sudo mkdir -p $SHADOWPATH
expect_good ${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv -- \
    "shadow logplay to existing shadow dir should succeed"
expect_good ${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv -- \
    "redo shadow logplay to existing shadow dir should succeed"

# Double shadow arg means re-parse yaml to test (if the shadow files are not already present)
sudo rm -rf $SHADOWPATH

expect_fail ${CLI} logplay --shadow $SHADOWPATH --shadow $SHADOWPATH --daxdev $DEV  -vv -- \
    "shadow logplay with yaml test with duplicate shadowpaths should fail"

#TODO: add some bad yaml to the yaml tree to test failures (or maybe do this in unit tests?

#
# Test cli 'famfs mount'
#
# Second mount causes fubar on 6.7, but fails as it should on 6.5 TODO: fix it!!
expect_good ${MOUNT} -vvv $DEV $MPT -- "famfs mount should succeed when not mounted"
expect_fail ${MOUNT} -vvv $DEV $MPT 2>/dev/null -- "famfs mount should fail when already mounted"

verify_mounted $DEV $MPT "test4.sh remount"

sudo mkdir ${SHADOWPATH}
expect_good ${CLI} logplay --shadow $SHADOWPATH --shadowtest $MPT  -vv -- \
    "shadow logplay from mounted meta files should succeed"

# check that a removed file is restored on remount
F="$MPT/test_xfile"
expect_good ${CLI} creat -s 16m -r -S 42 $F -- "failed to create F ($F)"
expect_fail sudo rm $F
expect_good sudo $UMOUNT $MPT            -- "umount failed"

verify_not_mounted $DEV $MPT "test4.sh 2nd umount"


expect_good ${MOUNT} -?             -- "famfs mount -? should succeed"
expect_fail ${MOUNT}                -- "famfs mount with no args should fail"
expect_fail ${MOUNT}  a b c         -- "famfs mount with too many args should fail"
expect_fail ${MOUNT} baddev $MPT    -- "famfs mount with bad device path should fail"
expect_fail ${MOUNT} $DEV badmpt    -- "famfs mount with bad mount point path should fail"


verify_not_mounted $DEV $MPT "test4.sh various bad mount attempts"

expect_fail ${MOUNT} -rm -vvv $DEV $MPT -- "famfs mount with -r and -m should fail"
expect_good ${MOUNT} -r -vvv $DEV $MPT  -- "famfs mount 2 should succeed when not mounted"
verify_mounted $DEV $MPT "test4.sh 2nd remount"

expect_good sudo test -f $F             -- "bogusly deleted file did not reappear on remount"
${CLI} verify -S 42 -f $F
expect_good sudo $UMOUNT $MPT            -- "umount should succeed"
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    if ((RMMOD > 0)); then
	expect_good sudo rmmod ${FAMFS_MOD}     -- "could not unload famfs when unmoounted"
	expect_fail ${MOUNT} -vvv $DEV $MPT -- "famfs mount should fail when kmod not loaded"
	expect_good sudo modprobe ${FAMFS_MOD}  -- "modprobe"
    fi
fi
expect_good ${MOUNT} -vv $DEV $MPT      -- "famfs mount should succeed after kmod reloaded"

#TODO troubleshoot remount
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    expect_good ${MOUNT} -R $DEV $MPT   -- "famfs mount -R should succeed when nothing is hinky"
fi
# mount -R needs mkmeta cleanup...

SHADOW_TARGET=/tmp/smoke.shadow
THIS_SHADOW=test4.shadow
SH=${SHADOW_TARGET}/${THIS_SHADOW}
expect_good mkdir -p ${THIS_SHADOW}/root -- "failed to mkdir -p ${SHADOW_TARGET}"
expect_good sudo rm -rf $SH -- "failed to remove $SH"
expect_good ${CLI} logplay -Ss $SH $MPT -- "shadow logplay should work"

sudo $UMOUNT $MPT # run_smoke.sh expects the file system unmounted after this test

set +x
echo ":==*************************************************************************"
echo ":==test4 (multichase) completed successfully"
echo ":==*************************************************************************"
exit 0
