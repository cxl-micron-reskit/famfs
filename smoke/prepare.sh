#!/usr/bin/env bash

TEST="prepare"

# Load header (defines: DEV, MPT, MKFS, FSCK, MOUNT, CLI, etc.)
source smoke/test_header.sh
# Load helper functions
source "$SCRIPTS/test_funcs.sh"

start_test $TEST

#set -x

#
# Ensure mountpoint exists
#
expect_good sudo mkdir -p "$MPT" -- "mkdir $MPT"

#
# Ensure famfs is NOT mounted at $MPT
#
# Unmount only if $DEV is actually mounted at $MPT
if findmnt -rn -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
    expect_good sudo umount "$MPT" -- "umount $MPT"
fi

# Also unmount temp fuse mount if present
stop_on_crash sudo umount /tmp/famfs_fuse -- "crashed umounting /tmp/famfs_fuse"

#
# Basic mkfs tests
#
expect_good "${MKFS[@]}" -h         -- "mkfs -h should work"
expect_fail "${MKFS[@]}"            -- "mkfs without dev argument should fail"
expect_fail "${MKFS[@]}" /tmp/nonexistent -- "mkfs on nonexistent dev should fail"

# If no filesystem is present, create one — ensure we do NOT crash
stop_on_crash "${MKFS[@]}" "$DEV" -- "Safety mkfs"

expect_fail "${MKFS[@]}" -k "$DEV"       -- "mkfs/kill should fail without --force"
expect_good "${MKFS[@]}" -f -k "$DEV"    -- "mkfs/kill should succeed with --force"
expect_good "${MKFS[@]}" "$DEV"          -- "mkfs"
expect_good "${MKFS[@]}" -f "$DEV"       -- "redo mkfs with -f should succeed"

#
# loglen sanity tests
#
expect_fail "${MKFS[@]}" -f --loglen 1 "$DEV" -- "mkfs with loglen 1 should fail"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "8388608" "Log size should not change after mkfs with bogus loglen"

expect_fail "${MKFS[@]}" -f --loglen 11m "$DEV" -- "mkfs with loglen 11m should fail"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "8388608" "Log size should not change after mkfs with bogus loglen 2"

# Valid log length
expect_good "${MKFS[@]}" -f --loglen 256m "$DEV" -- "mkfs should work with 256m log"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "268435456" "Log should be 256M"

expect_fail "${MKFS[@]}" "$DEV" -- "redo mkfs without -f should fail"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "268435456" "Log size should not change after failed mkfs"

expect_fail "${MKFS[@]}" -f --loglen 1m "$DEV" -- "mkfs should fail with 1m logsize"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "268435456" "Log size should not change after mkfs with bogus loglen 3"

expect_good "${MKFS[@]}" -f "$DEV" -- "redo mkfs with -f should succeed 2"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "8388608" "Log size should revert after -f mkfs"

expect_fail "${MKFS[@]}" "$DEV" -- "mkfs redo should fail when fs exists"

#
# CLI & FSCK sanity
#
expect_good "${CLI[@]}" -h        -- "cli -h should succeed"
expect_good "${FSCK[@]}" "$DEV"   -- "fsck"

#
# Mount testing differs by mode
#

if [[ "$FAMFS_MODE" == "v1" ]]; then
    # Ensure module is loaded
    expect_good sudo modprobe "$FAMFS_MOD" -- "modprobe ${FAMFS_MOD}"

    #
    # Test manual mount / mkmeta / logplay
    #
    expect_good sudo mount "${RAW_MOUNT_OPTS[@]}" "$DEV" "$MPT" -- "raw mount"
    expect_fail sudo mount "${RAW_MOUNT_OPTS[@]}" "$DEV" "$MPT" -- "double raw mount should fail"

    expect_good "${CLI[@]}" mkmeta "$DEV" -- "mkmeta"

    # Make sure the mount appears in /proc/mounts
    expect_good grep "$DEV" /proc/mounts -- "dev=$DEV not in /proc/mounts"

else
    # FUSE mode mount
    expect_good "${MOUNT[@]}" -vv "$DEV" "$MPT" -- "famfs fuse mount should work"
fi

expect_good grep famfs /proc/mounts -- "No famfs mounted"
expect_good grep "$MPT" /proc/mounts -- "mount point $MPT not in /proc/mounts"

expect_good sudo test -f "$MPT/.meta/.superblock" -- "no superblock file after mkmeta"
expect_good sudo test -f "$MPT/.meta/.log"        -- "prep: no log file after mkmeta"

expect_good "${CLI[@]}" logplay "$MPT"  -- "empty fs logplay should succeed"
expect_good "${FSCK[@]}" --human "$MPT" -- "prep: fsck --human should succeed"

#
# Try mkfs while mounted (should fail)
#
expect_fail "${MKFS[@]}" "$DEV" -- "mkfs while mounted should fail"

#
# Debug ICACHE stats if fuse mode
#
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    expect_good sudo curl --unix-socket \
        "$(scripts/famfs_shadow.sh /mnt/famfs)/sock" \
        http://localhost/icache_stats \
        -- "REST query failed"
fi

#
# Test mount with invalid superblock (after kill)
#
expect_good sudo umount "$MPT" -- "umount $MPT should succeed"
verify_not_mounted "$DEV" "$MPT" "umount failed?"

expect_good "${MKFS[@]}" -f "$DEV"    -- "mkfs should succeed with --force"
expect_good "${MKFS[@]}" -f -k "$DEV" -- "mkfs/kill should succeed with --force (2)"

# mount should now fail with bad superblock
expect_fail_except --bad 99 \
    "${MOUNT[@]}" "$DEV" "$MPT" -- "mount should fail with bad superblock"

verify_not_mounted "$DEV" "$MPT" "should not be mounted after failed mount"

# create a clean filesystem
expect_good "${MKFS[@]}" "$DEV" -- "clean mkfs should succeed"

#
# Mount without specifying fuse/no-fuse; let CLI auto-detect
#
expect_good "${CLI[@]}" mount -v "${MOUNT_OPTS[@]}" "$DEV" "$MPT" \
    -- "CLI auto mount should work"
expect_good sudo umount "$MPT" -- "umount $MPT should succeed"

verify_not_mounted "$DEV" "$MPT" "umount failed?"

#
# Final mount tests
#
expect_good "${MOUNT[@]}" "$DEV" "$MPT" -- "mount of clean file system should succeed"
expect_fail "${MOUNT[@]}" "$DEV" "$MPT" -- "Double mount should fail"

verify_mounted "$DEV" "$MPT" "mount failed?"

set +x
echo ":==*************************************************************************"
echo ":==prepare completed successfully"
echo ":==*************************************************************************"
exit 0
