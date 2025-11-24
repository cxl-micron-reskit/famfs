#!/usr/bin/env bash

source ./test_header.sh

TEST="test_errors:"

source $SCRIPTS/test_funcs.sh

if [[ "${FAMFS_MODE}" == "fuse" ]]; then
    echo "*************************************************************"
    echo "test_errs does not support famfs/fuse yet"
    echo "*************************************************************"
    sleep 1
    exit 0
fi

set -x

# Start with a clean, empty file system
famfs_recreate "test_errors"

verify_mounted $DEV $MPT "test_errors.sh"

expect_good ${FSCK} $MPT -- "fsck should not fail when nothing cloned"

# Create a file to clone
expect_good ${CLI} creat -r -S 10 -s 0x400000 $MPT/original -- "create original should succeed"
expect_good ${CLI} verify -S 10 -f $MPT/original            -- "verify original should succeed"

N="10"
FILE="bigtest$N"
expect_good ${CLI} clone -h                               -- "clone -h should succeed"
expect_good ${CLI} clone $MPT/original $MPT/clone         -- "clone original should succeed"
expect_good ${CLI} clone -v $MPT/original $MPT/clone1     -- "second clone should succeed"
expect_good ${CLI} clone -v $MPT/clone $MPT/clone2        -- "clone from clone should also succeed"

expect_good ${CLI} verify -S 10 -f $MPT/clone             -- "verify clone should succeed"
expect_good ${CLI} verify -S 10 -f $MPT/clone1            -- "verify clone1 should succeed"
expect_good ${CLI} verify -S 10 -f $MPT/clone2            -- "verify clone2 should succeed"


# Error cases
expect_fail ${CLI} clone  -- "clone with no args should fail"
expect_fail ${CLI} clone -v $MPT/bogusfile $MPT/bogusfile.cllone -- "clone bogusfile should fail"
expect_fail ${CLI} clone -v /etc/passwd $MPT/passwd              -- "clone from outside famfs should fail"


expect_fail ${FSCK} $MPT -- "fsck should fail after cloning "
expect_good ${CLI} verify -S 10 -f $MPT/clone  -- "re-verify clone"
expect_good ${CLI} verify -S 10 -f $MPT/clone1 -- "re-verify clone1"

expect_good sudo $UMOUNT $MPT -- "umount"
verify_not_mounted $DEV $MPT "test_errors .sh"
expect_good ${MOUNT} $DEV $MPT -- "mount in test_errors"
#full_mount $DEV $MPT "${MOUNT_OPTS}" "test_errors.sh"
verify_mounted $DEV $MPT "test1.sh"

# Throw a curveball or two at logplay
expect_good ${CLI} mkdir $MPT/adir         -- "should be able to mkdir adir"
expect_good ${CLI} mkdir $MPT/adir2        -- "should be able to mkdir adir2"
sudo rm -rf $MPT/adir
if (( $? == 0 )); then
    expect_good sudo touch $MPT/adir           -- "should be able to create rogue file"
    expect_good sudo rm -rf $MPT/adir2         \
	-- "should be able to rogue remove a dir2"
    expect_good sudo touch $MPT/adir2          -- "should be able to touch a file"
    expect_fail ${CLI} logplay -vvv $MPT       \
	-- "logplay should complain when a file is where a dir should be"
fi
expect_fail sudo ln -s /tmp $MPT/adir2     -- "symlink should fail with famfs kmod v2"

expect_fail ${FSCK} -v $MPT -- "fsck -v if a clone has ever happened should fail"
expect_fail ${FSCK} $MPT -- "fsck if a clone has ever happened should fail"

# Some v2-specific tests

expect_good sudo $UMOUNT $MPT            -- "umount should work"
expect_good ${MOUNT} $DEV $MPT       -- "basic mount should succeed"
expect_fail ${MOUNT} $DEV $MPT       -- "remount 1 should fail"
expect_fail ${MOUNT} $DEV $MPT       -- "remount 2 should fail"
expect_fail ${MOUNT} $DEV $MPT       -- "remount 3 should fail"
sudo mkdir -p /tmp/famfs
expect_fail ${MOUNT} $DEV /tmp/famfs -- "remount at different path should fail"
verify_mounted $DEV $MPT     "mounted after redundant mounts"
expect_good sudo $UMOUNT $MPT            -- "umount should work after redundant mounts"
verify_not_mounted $DEV $MPT "umount should have worked after redundant mounts"

expect_good ${MOUNT} $DEV $MPT       -- "basic mount should succeed"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_errors.shadow $MPT

#sudo $UMOUNT $MPT || fail "umount"

set +x
echo "*************************************************************************"
echo " Important note: This test (at least the first run) will generate a stack dump"
echo " in the kernel log (a WARN_ONCE) due to cross-linked pages (specifically DAX noticing"
echo " that a page was mapped to more than one file. This is normal, as this test intentionally"
echo " does bogus cross-linked mappings"
set +x
echo ":==*************************************************************************"
echo ":==test_errors completed successfully"
echo ":==*************************************************************************"
exit 0
