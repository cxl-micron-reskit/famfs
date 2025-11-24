#!/usr/bin/env bash

source smoke/test_header.sh

TEST="prepare"

source $SCRIPTS/test_funcs.sh

#set -x

sudo mkdir -p $MPT || fail "mkdir $MPT"

# Make sure famfs is not mounted
findmnt $MPT
if (( $? == 0 )); then
    sudo umount $MPT
fi
sudo umount /tmp/famfs_fuse

# destroy famfs file system, if any
expect_good ${MKFS} -h    -- "mkfs -h should work"
expect_fail ${MKFS}       -- "mkfs without dev argument should fail"

expect_fail ${MKFS} /tmp/nonexistent -- "mkfs on nonexistent dev should fail"

# in case there is not a file system, make one. Failure OK, but not crashes
stop_on_crash ${MKFS} $DEV -- "Safety mkfs"

expect_fail ${MKFS} -k $DEV -- "mkfs/kill should fail without --force"
expect_good ${MKFS} -f -k $DEV -- "mkfs/kill should succeed with --force"
expect_good ${MKFS}  $DEV      -- "mkfs"
expect_good ${MKFS} -f $DEV    -- "redo mkfs with -f should succeed"

expect_fail ${MKFS} -f --loglen 1 $DEV -- "mkfs with loglen 1 should fail"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen"

${MKFS} -f --loglen 11m $DEV && fail "mkfs with loglen 11m should fail"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen 2"

#exit 1
${MKFS} -f --loglen 256m $DEV || fail "mkfs should work with 256m log"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log should be 256M"

${MKFS} $DEV && fail "redo mkfs without -f should fail"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log size should not change after failed mkfs"

${MKFS} -f --loglen 1m $DEV   && fail "mkfs should fail with 1m logsize"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log size should not change after mkfs with bogus loglen 3"

${MKFS} -f $DEV       || fail "redo mkfs with -f should succeed 2"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen4"

expect_fail ${MKFS}  $DEV   -- "mkfs redo" # fail, fs exists

expect_good ${CLI} -h        -- "cli -h should succeed"
expect_good ${FSCK} $DEV -- "fsck"

if [[ "$FAMFS_MODE" == "v1" ]]; then
    # We now expect the module to already be loaded (if FAMFS_MODE==v1),
    # but no harm in modprobe to make double sure
    expect_good sudo modprobe ${FAMFS_MOD} -- "modprobe ${FAMFS_MOD}"

    #
    # Test manual mount / mkmeta / logplay
    #
    expect_good sudo mount $RAW_MOUNT_OPTS $DEV $MPT -- "mount"
    expect_fail sudo mount $RAW_MOUNT_OPTS $DEV $MPT -- "double mount should fail"

    expect_good ${CLI} mkmeta $DEV -- "mkmeta"

    # XXX famfs-fuse does not yet put the primary daxdev in /proc/mounts.
    # need to fix this
    expect_good grep $DEV /proc/mounts   -- "dev=$DEV not in /proc/mounts~"
else
    # In fuse mode, we don't support manual mount...
    expect_good ${MOUNT} -vv $DEV $MPT -- "famfs fuse mount should work"
fi

expect_good grep famfs /proc/mounts -- "No famfs mounted"
expect_good grep $MPT /proc/mounts  -- "Mount pt $MPT not in /proc/mounts~"

expect_good sudo test -f $MPT/.meta/.superblock -- "no superblock file after mkmeta"
expect_good sudo test -f $MPT/.meta/.log -- "prep: no log file after mkmeta"

expect_good ${CLI} logplay $MPT  -- "empty fs logplay should succeed"
expect_good ${FSCK} --human $MPT -- "prep: fsck --human should succeed"

# Try mkfs while mounted
expect_fail ${MKFS}  $DEV        -- "mkfs while mounted should fail"

# XXX: currently after fuse mount, daxdev is not open unless a file has
# been looked up and accessed.
#exit -1

#${MKFS} -f -k $DEV    && fail "mkfs/kill while mounted should fail"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    expect_good sudo curl  --unix-socket \
		$(scripts/famfs_shadow.sh /mnt/famfs)/sock \
		http://localhost/icache_stats -- "REST query failed"
fi

#
# Blow away the file system and test famfs mount with no valid superblock
#
expect_good sudo umount $MPT       -- "umount $MPT should succeed"
verify_not_mounted $DEV $MPT "umount failed?"

# We don't know for certain whether there is a valid file system, so create one...
expect_good ${MKFS} -f $DEV        -- "mkfs should succeed with --force"
expect_good ${MKFS} -f -k $DEV     -- "mkfs/kill should succeed with --force (2)"
# Now there is not a valid file system because we killed the superblock...
expect_fail_except --bad 99 \
		   ${MOUNT} $DEV $MPT -- "mount should fail with bad superblock"
verify_not_mounted $DEV $MPT "should not be mounted after failed mount"
expect_good ${MKFS} $DEV           -- "clean mkfs should succeed"

# Mount without specifying fuse or otherwise; famfs should figure it out
expect_good ${CLI} mount -v $MOUNT_OPTS $DEV $MPT -- "mount with unspecified type should work"
expect_good sudo umount $MPT       -- "umount $MPT should succeed"
verify_not_mounted $DEV $MPT "umount failed?"

expect_good ${MOUNT} $DEV $MPT     -- "mount of clean file system should succeed"
expect_fail ${MOUNT} $DEV $MPT     -- "Double mount should fail "

verify_mounted $DEV $MPT "mount failed?"

set +x
echo ":==*************************************************************************"
echo ":==prepare completed successfully"
echo ":==*************************************************************************"
exit 0
