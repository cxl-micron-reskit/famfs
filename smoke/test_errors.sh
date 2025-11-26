#!/usr/bin/env bash

set -euo pipefail

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

TEST="test_errors"
start_test $TEST

# test_errors only works with famfsv1 (not fuse)
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    echo "*************************************************************"
    echo "test_errors does not support famfs/fuse yet"
    echo "*************************************************************"
    sleep 1
    exit 0
fi

#
# Fresh filesystem
#
famfs_recreate "test_errors"
verify_mounted "$DEV" "$MPT" "test_errors.sh"

expect_good   "${FSCK[@]}" "$MPT" \
             -- "fsck should not fail when nothing cloned"

#
# Create a file to clone
#
expect_good   "${CLI[@]}" creat -r -S 10 -s 0x400000 "$MPT/original" \
             -- "create original"
expect_good   "${CLI[@]}" verify -S 10 -f "$MPT/original" \
             -- "verify original"

#
# Basic clone tests
#
expect_good   "${CLI[@]}" clone -h \
             -- "clone -h should succeed"

expect_good   "${CLI[@]}" clone "$MPT/original" "$MPT/clone" \
             -- "clone original"

expect_good   "${CLI[@]}" clone -v "$MPT/original" "$MPT/clone1" \
             -- "clone1"

expect_good   "${CLI[@]}" clone -v "$MPT/clone" "$MPT/clone2" \
             -- "clone2"

expect_good   "${CLI[@]}" verify -S 10 -f "$MPT/clone"  -- "verify clone"
expect_good   "${CLI[@]}" verify -S 10 -f "$MPT/clone1" -- "verify clone1"
expect_good   "${CLI[@]}" verify -S 10 -f "$MPT/clone2" -- "verify clone2"

#
# Error cases
#
expect_fail   "${CLI[@]}" clone \
             -- "clone with no args should fail"

expect_fail   "${CLI[@]}" clone -v "$MPT/bogusfile" "$MPT/bogusfile.cllone" \
             -- "clone on nonexistent file should fail"

expect_fail   "${CLI[@]}" clone -v /etc/passwd "$MPT/passwd" \
             -- "clone from non-famfs file should fail"

#
# fsck should now fail after cloning
#
expect_fail   "${FSCK[@]}" "$MPT" \
             -- "fsck should fail after cloning"

expect_good   "${CLI[@]}" verify -S 10 -f "$MPT/clone"  -- "reverify clone"
expect_good   "${CLI[@]}" verify -S 10 -f "$MPT/clone1" -- "reverify clone1"

#
# Unmount/remount
#
expect_good   sudo "$UMOUNT" "$MPT" -- "umount"
verify_not_mounted "$DEV" "$MPT" "test_errors.sh"

expect_good   "${MOUNT[@]}" "$DEV" "$MPT" \
             -- "mount after unmount"
verify_mounted "$DEV" "$MPT" "test_errors remount"

#
# Throw curveballs at logplay
#
expect_good   "${CLI[@]}" mkdir "$MPT/adir"  -- "mkdir adir"
expect_good   "${CLI[@]}" mkdir "$MPT/adir2" -- "mkdir adir2"

set +e
sudo rm -rf "$MPT/adir"
rc=$?
set -e
if (( rc == 0 )); then
    expect_good sudo touch "$MPT/adir"     -- "rogue file in place of dir"
    expect_good sudo rm -rf "$MPT/adir2"   -- "remove adir2"
    expect_good sudo touch "$MPT/adir2"    -- "touch file"
    expect_fail "${CLI[@]}" logplay -vvv "$MPT" \
                -- "logplay should complain when file replaces directory"
fi

expect_fail sudo ln -s /tmp "$MPT/adir2" \
            -- "symlink should fail with famfs v1/v2"

expect_fail "${FSCK[@]}" -v "$MPT" \
            -- "fsck -v should fail once cloning has occurred"
expect_fail "${FSCK[@]}" "$MPT" \
            -- "fsck should fail after cloning"

#
# v2-specific redundant mount behavior (also applies to v1 logically)
#
expect_good sudo "$UMOUNT" "$MPT" -- "umount before remount tests"
expect_good "${MOUNT[@]}" "$DEV" "$MPT" -- "normal mount"

expect_fail "${MOUNT[@]}" "$DEV" "$MPT" -- "redundant remount 1"
expect_fail "${MOUNT[@]}" "$DEV" "$MPT" -- "redundant remount 2"
expect_fail "${MOUNT[@]}" "$DEV" "$MPT" -- "redundant remount 3"

sudo mkdir -p /tmp/famfs
expect_fail "${MOUNT[@]}" "$DEV" /tmp/famfs \
            -- "mount same dev at different path should fail"

verify_mounted "$DEV" "$MPT" "after redundant mount attempts"

expect_good sudo "$UMOUNT" "$MPT" \
            -- "umount after redundant mounts"
verify_not_mounted "$DEV" "$MPT" "should be unmounted"

expect_good "${MOUNT[@]}" "$DEV" "$MPT" \
            -- "mount after redundant tests"

#
# shadow logplay
#
SHADOW="/tmp/smoke.shadow/test_errors.shadow/root"

expect_good sudo mkdir -p "$SHADOW" \
            -- "mkdir shadow path"

expect_good "${CLI[@]}" logplay -sS "$SHADOW" "$MPT" \
            -- "shadow logplay -Ss should work"

set +x
echo "*************************************************************************"
echo " NOTE: This test *intentionally* triggers kernel WARN_ONCE stack dumps"
echo "       due to forced cross-linked DAX mappings. This is normal."
echo "*************************************************************************"
finish_test $TEST
exit 0
