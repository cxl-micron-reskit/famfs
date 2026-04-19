#!/usr/bin/env bash

TEST="test_load_module"

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

start_test $TEST

#
# This script tests the --load-module / -L flag for 'famfs mount' and 'famfs fsck'.
#
# The flag is V1-only: it calls modprobe before the famfs_module_loaded() safety
# guard, allowing the operator to mount or check a filesystem without manually
# loading the module first.
#
# Prerequisites:
#   - FAMFS_MODE=v1  (FUSE mounts don't use the kernel module)
#   - RMMOD=1        (test harness -n flag; enables module unload tests)
#

if [[ "$FAMFS_MODE" != "v1" ]]; then
    echo ":== test_load_module: skipping (FAMFS_MODE=$FAMFS_MODE, not v1)"
    finish_test $TEST
    exit 0
fi

if (( RMMOD == 0 )); then
    echo ":== test_load_module: skipping (module unload not enabled; pass -n to run_smoke.sh)"
    finish_test $TEST
    exit 0
fi

# Identify the currently-loaded famfs module name (famfs or famfsv1)
LOADED_MOD=$(lsmod | awk '$1 ~ /^famfs/ { print $1; exit }')
if [[ -z "$LOADED_MOD" ]]; then
    echo ":==FAIL: test_load_module: no famfs module found in lsmod"
    exit 1
fi
echo ":= test_load_module: famfs module in use: $LOADED_MOD"

#
# Setup: fresh filesystem so there is a valid sb+log for fsck to read
#
famfs_recreate "test_load_module setup"
expect_good sudo umount "$MPT" -- "test_load_module: umount after setup"
verify_not_mounted "$DEV" "$MPT" "test_load_module: still mounted after umount?"

# ==========================================================================
# Test 1: famfs mount --load-module
# ==========================================================================
echo ""
echo ":=== test_load_module: TEST 1: famfs mount --load-module ==="

expect_good sudo rmmod "$LOADED_MOD" \
    -- "test_load_module: unload $LOADED_MOD before mount test"

expect_fail "${MOUNT[@]}" "$DEV" "$MPT" \
    -- "test_load_module: mount without --load-module should fail (module unloaded)"

expect_good "${MOUNT[@]}" --load-module "$DEV" "$MPT" \
    -- "test_load_module: mount --load-module should reload module and succeed"

verify_mounted "$DEV" "$MPT" \
    "test_load_module: filesystem not mounted after --load-module?"

expect_good sudo umount "$MPT" -- "test_load_module: umount after mount --load-module test"
verify_not_mounted "$DEV" "$MPT" "test_load_module: still mounted after umount?"

# ==========================================================================
# Test 2: famfs fsck --load-module
# ==========================================================================
echo ""
echo ":=== test_load_module: TEST 2: famfs fsck --load-module ==="

expect_good sudo rmmod "$LOADED_MOD" \
    -- "test_load_module: unload $LOADED_MOD before fsck test"

expect_fail "${FSCK[@]}" "$DEV" \
    -- "test_load_module: fsck without --load-module should fail (module unloaded)"

expect_good "${FSCK[@]}" --load-module "$DEV" \
    -- "test_load_module: fsck --load-module should reload module and succeed"

# Restore: remount so subsequent smoke tests start from a mounted state
"${MOUNT[@]}" "$DEV" "$MPT" || true
verify_mounted "$DEV" "$MPT" "test_load_module: remount after fsck test"

echo ":==*************************************************************************"
echo ":==test_load_module completed successfully"
echo ":==*************************************************************************"
finish_test $TEST
exit 0
