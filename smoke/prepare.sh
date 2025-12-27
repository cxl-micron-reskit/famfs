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
# Ensure $DEV is NOT mounted anywhere, and $MPT is available
#
# Fail if a different device is mounted exactly at $MPT
# Note: don't use --target (which traverses up to parent mounts)
mounted_dev=$(findmnt -rn -o SOURCE "$MPT" 2>/dev/null || true)
if [[ -n "$mounted_dev" && "$mounted_dev" != "$DEV" ]]; then
    echo "ERROR: $MPT is in use by a different device: $mounted_dev"
    exit 1
fi

# Unmount $DEV from anywhere it might be mounted
for mpt in $(findmnt -rn -S "$DEV" -o TARGET 2>/dev/null); do
    echo "Unmounting $DEV from $mpt"
    sudo umount "$mpt" 2>/dev/null || true
done

# Also unmount temp fuse mount if present
stop_on_crash sudo umount /tmp/famfs_fuse -- "crashed umounting /tmp/famfs_fuse"

DAXMODE=$(dax_get_mode $DEV "prepare 1")
echo "DAXMODE: $DAXMODE"

# Ensure we start in devdax mode for consistent test behavior
if [[ "$DAXMODE" != "devdax" ]]; then
    dax_reconfigure_mode $DEV "devdax"
    DAXMODE="devdax"
fi

#
# Basic mkfs tests
#
expect_good "${MKFS[@]}" -h         -- "mkfs -h should work"
assert_daxmode_6.19 $DEV $DAXMODE "1"
expect_fail "${MKFS[@]}"            -- "mkfs without dev argument should fail"
assert_daxmode_6.19 $DEV $DAXMODE "2"
expect_fail "${MKFS[@]}" /tmp/nonexistent -- "mkfs on nonexistent dev should fail"
assert_daxmode_6.19 $DEV $DAXMODE "3"

# If no filesystem is present, create one — ensure we do NOT crash
stop_on_crash "${MKFS[@]}" "$DEV" -- "Safety mkfs"
dax_reconfigure_mode $DEV "devdax"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs?"
expect_fail "${MKFS[@]}" -k "$DEV"       -- "mkfs/kill should fail without --force"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (2)"
expect_good "${MKFS[@]}" -f -k "$DEV"    -- "mkfs/kill should succeed with --force"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (3)"
expect_good "${MKFS[@]}" "$DEV"          -- "mkfs"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (4)"
expect_good "${MKFS[@]}" -v -f "$DEV"       -- "redo mkfs with -f should succeed"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (5)"

#
# Set daxdev mode back to devdax
#
dax_reconfigure_mode $DEV "devdax"

#
# loglen sanity tests
#
expect_fail "${MKFS[@]}" -f --loglen 1 "$DEV" -- "mkfs with loglen 1 should fail"
# mkfs fails early (before mode switch) due to invalid loglen, so mode unchanged
assert_daxmode_6.19 $DEV "devdax" "5"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "8388608" "Log size should not change after mkfs with bogus loglen"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (6)"

# raw fsck above will changed to mode=famfs, but should put it back to devdax
assert_daxmode_6.19 $DEV "devdax" "7"


expect_fail "${MKFS[@]}" -f --loglen 11m "$DEV" -- "mkfs with loglen 11m should fail"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "8388608" "Log size should not change after mkfs with bogus loglen 2"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (7)"

# mkfs should have changed the mode and not set it back (b/c if you made a
# famfs file system, you probably want it in famfs mode...duh...
assert_daxmode_6.19 $DEV "devdax" "8"

# Valid log length
expect_good "${MKFS[@]}" -f --loglen 256m "$DEV" -- "mkfs should work with 256m log"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "268435456" "Log should be 256M"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (8)"

expect_fail "${MKFS[@]}" "$DEV" -- "redo mkfs without -f should fail"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "268435456" "Log size should not change after failed mkfs"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (9)"

expect_fail "${MKFS[@]}" -f --loglen 1m "$DEV" -- "mkfs should fail with 1m logsize"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "268435456" "Log size should not change after mkfs with bogus loglen 3"

expect_good "${MKFS[@]}" -f "$DEV" -- "redo mkfs with -f should succeed 2"
LOG_LEN=$(expect_good "${FSCK[@]}" -v "$DEV" | grep "log_len" | awk '{print $2}')
assert_equal "$LOG_LEN" "8388608" "Log size should revert after -f mkfs"

expect_fail "${MKFS[@]}" "$DEV" -- "mkfs redo should fail when fs exists"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (10)"

#
# CLI & FSCK sanity
#
expect_good "${CLI[@]}" -h        -- "cli -h should succeed"
expect_good "${FSCK[@]}" "$DEV"   -- "fsck"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after fsck"

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
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (11)"
expect_good "${MKFS[@]}" -f -k "$DEV" -- "mkfs/kill should succeed with --force (2)"

# mount should now fail with bad superblock
expect_fail_except --bad 99 \
    "${MOUNT[@]}" "$DEV" "$MPT" -- "mount should fail with bad superblock"
verify_not_mounted "$DEV" "$MPT" "should not be mounted after failed mount"

# create a clean filesystem
expect_good "${MKFS[@]}" "$DEV" -- "clean mkfs should succeed"
verify_dev_not_mounted $DEV "$DEV lingering dummy mount after mkfs? (12)"

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
