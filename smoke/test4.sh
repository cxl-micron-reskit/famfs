#!/usr/bin/env bash

set -euo pipefail

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

TEST="test4"
start_test $TEST

# multichase binary (array form)
MULTICHASE=(sudo "$BIN/src/multichase/multichase")

# Start with a clean, empty file system
famfs_recreate "test4"
verify_mounted "$DEV" "$MPT" "test4.sh"

expect_fail   "${CLI[@]}" badarg                        -- "create badarg should fail"
expect_good   "${CLI[@]}" creat -h                      -- "creat -h should succeed"
expect_good   "${CLI[@]}" creat -s 3g      "$MPT/memfile"   -- "create memfile for multichase"
expect_good   "${CLI[@]}" creat -s 100m    "$MPT/memfile1"  -- "creat should succeed with -s 100m"
expect_good   "${CLI[@]}" creat -s 10000k  "$MPT/memfile2"  -- "creat with -s 10000k should succeed"

expect_good   "${MULTICHASE[@]}" -d "$MPT/memfile" -m 2900m \
              -- "multichase run"

verify_mounted "$DEV" "$MPT" "test4.sh mounted"
expect_good sudo "$UMOUNT" "$MPT"                        -- "umount"
verify_not_mounted "$DEV" "$MPT" "test4.sh"

#
# Shadow logplay tests while unmounted
#
SHADOWPATH=/tmp/shadowpath/root

expect_fail   "${CLI[@]}" logplay --shadow -d /dev/bogodax \
              -- "shadow logplay should fail with bogus daxdev"

sudo rm -rf "$SHADOWPATH"

expect_fail   "${CLI[@]}" logplay --shadow "$SHADOWPATH/frob" --daxdev "$DEV" -vv \
              -- "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"

expect_fail   "${CLI[@]}" logplay --daxdev "$DEV" -vv "$SHADOWPATH" \
              -- "logplay should fail if --daxdev is set without --shadow"

sudo rm -rf "$SHADOWPATH"
sudo mkdir -p "$SHADOWPATH"

expect_good   "${CLI[@]}" logplay --shadow "$SHADOWPATH" --daxdev "$DEV" -vv \
              -- "shadow logplay to existing shadow dir should succeed"

expect_good   "${CLI[@]}" logplay --shadow "$SHADOWPATH" --daxdev "$DEV" -vv \
              -- "redo shadow logplay should also succeed"

sudo rm -rf "$SHADOWPATH"

expect_fail   "${CLI[@]}" logplay --shadow "$SHADOWPATH" --shadow "$SHADOWPATH" \
              --daxdev "$DEV" -vv \
              -- "duplicate --shadow should fail"

#
# famfs mount tests
#
expect_good   "${MOUNT[@]}" -vvv "$DEV" "$MPT" -- "mount should succeed"
expect_fail   "${MOUNT[@]}" -vvv "$DEV" "$MPT" \
              -- "mount should fail when already mounted"

verify_mounted "$DEV" "$MPT" "test4.sh remount"

sudo mkdir -p "$SHADOWPATH"
expect_good   "${CLI[@]}" logplay --shadow "$SHADOWPATH" --shadowtest "$MPT" -vv \
              -- "shadow logplay from mounted metadata"

#
# File deletion / restoration test
#
F="$MPT/test_xfile"
expect_good   "${CLI[@]}" creat -s 16m -r -S 42 "$F" -- "failed to create F"
expect_fail   sudo rm "$F"

expect_good   sudo "$UMOUNT" "$MPT" -- "umount"
verify_not_mounted "$DEV" "$MPT" "test4.sh second umount"

#
# mount argument validation
#
expect_good   "${MOUNT[@]}" -?    -- "mount -? should succeed"
expect_fail   "${MOUNT[@]}"       -- "mount with no args should fail"
expect_fail   "${MOUNT[@]}" a b c -- "mount with too many args should fail"
expect_fail   "${MOUNT[@]}" baddev "$MPT" -- "bad device"
expect_fail   "${MOUNT[@]}" "$DEV" badmpt -- "bad mountpoint"

verify_not_mounted "$DEV" "$MPT" "test4 bad mount attempts"

expect_fail   "${MOUNT[@]}" -rm -vvv "$DEV" "$MPT" \
              -- "mount with -r and -m should fail"

expect_good   "${MOUNT[@]}" -r -vvv "$DEV" "$MPT" \
              -- "mount -r should succeed"

verify_mounted "$DEV" "$MPT" "test4.sh 2nd remount"

expect_good   sudo test -f "$F" \
              -- "deleted file restored after remount"

expect_good sudo "$UMOUNT" "$MPT" -- "final umount"

#
# kmod unload tests (v1 only)
#
if [[ "$FAMFS_MODE" == "v1" ]]; then
    if (( RMMOD > 0 )); then
        expect_good sudo rmmod "$FAMFS_MOD"  -- "unload famfs"
        expect_fail "${MOUNT[@]}" -vvv "$DEV" "$MPT" \
                    -- "mount should fail when module is unloaded"
        expect_good sudo modprobe "$FAMFS_MOD" -- "reload module"
    fi
fi

expect_good "${MOUNT[@]}" -vv "$DEV" "$MPT" \
            -- "mount after reload"

if [[ "$FAMFS_MODE" == "v1" ]]; then
    expect_good "${MOUNT[@]}" -R "$DEV" "$MPT" \
                -- "mount -R should succeed when clean"
fi

#
# final shadow logplay
#
SHADOW_TARGET=/tmp/smoke.shadow
THIS_SHADOW=test4.shadow
SH="$SHADOW_TARGET/$THIS_SHADOW"

expect_good sudo mkdir -p "$SH/root"   -- "mkdir shadow tree"

expect_good sudo rm -rf "$SH"          -- "cleanup old shadow"

expect_good "${CLI[@]}" logplay -Ss "$SH" "$MPT" \
            -- "shadow logplay should work"

sudo "$UMOUNT" "$MPT"

set +x
finish_test $TEST
exit 0
