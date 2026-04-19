#!/usr/bin/env bash

TEST="test_daxmode"

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

start_test $TEST

#
# This script tests the --set-daxmode flag for famfs mount, fsck, and logplay.
# mkfs --set-daxmode is tested in prepare.sh (sniff path).
#
# Each test:
#   1. Uses daxctl to put the device in devdax mode (precondition)
#   2. Runs the famfs command with --set-daxmode
#   3. Asserts the device is now in famfs mode and the command succeeded
#
# Only runs on kernel >= 7.0; earlier kernels do not require famfs mode.
#

if [[ "$KERNEL_MAJOR" -lt 7 ]]; then
    echo ":== test_daxmode: skipping (kernel $KERNEL_VERSION < 7.0, famfs mode not required)"
    finish_test $TEST
    exit 0
fi

DAXCTL=$(get_daxctl_path)
if [[ ! -x "$DAXCTL" ]]; then
    echo ":==FAIL: test_daxmode: daxctl not found (checked: $DAXCTL)"
    echo ":==      Build ndctl or set DAXCTL_PATH"
    exit 1
fi
echo ":= test_daxmode: using daxctl: $DAXCTL"

SHADOWPATH=/tmp/test_daxmode.shadow

#
# Setup: fresh filesystem with a few files for logplay to replay
#
famfs_recreate "test_daxmode setup"

expect_good sudo mkdir -p "$SHADOWPATH/root" -- "test_daxmode: create shadow dir"

expect_good "${CLI[@]}" creat -r -s 1m -S 101 "$MPT/daxmode_file1" \
    -- "test_daxmode: create file1"
expect_good "${CLI[@]}" creat -r -s 1m -S 202 "$MPT/daxmode_file2" \
    -- "test_daxmode: create file2"

expect_good sudo umount "$MPT" -- "test_daxmode: umount after setup"
verify_not_mounted "$DEV" "$MPT" "test_daxmode: still mounted after umount?"

#
# Test 1: famfs mount --set-daxmode
#
echo ""
echo ":=== test_daxmode: TEST 1: famfs mount --set-daxmode ==="
dax_reconfigure_mode "$DEV" "devdax"
assert_daxmode_6.19 "$DEV" "devdax" "test_daxmode: pre-condition devdax for mount test"

expect_good "${MOUNT[@]}" --set-daxmode "$DEV" "$MPT" \
    -- "test_daxmode: mount --set-daxmode should switch to famfs and mount"
assert_daxmode_6.19 "$DEV" "famfs" "test_daxmode: device should be famfs after mount --set-daxmode"
verify_mounted "$DEV" "$MPT" "test_daxmode: mount --set-daxmode: filesystem not mounted?"

expect_good sudo umount "$MPT" -- "test_daxmode: umount after mount --set-daxmode test"
verify_not_mounted "$DEV" "$MPT" "test_daxmode: still mounted after umount?"

#
# Test 2: famfs fsck --set-daxmode
#
echo ""
echo ":=== test_daxmode: TEST 2: famfs fsck --set-daxmode ==="
dax_reconfigure_mode "$DEV" "devdax"
assert_daxmode_6.19 "$DEV" "devdax" "test_daxmode: pre-condition devdax for fsck test"

expect_good "${FSCK[@]}" --set-daxmode "$DEV" \
    -- "test_daxmode: fsck --set-daxmode should switch to famfs and check"
assert_daxmode_6.19 "$DEV" "famfs" "test_daxmode: device should be famfs after fsck --set-daxmode"

#
# Test 3: famfs logplay --shadow --set-daxmode
#
echo ""
echo ":=== test_daxmode: TEST 3: famfs logplay --shadow --set-daxmode ==="
dax_reconfigure_mode "$DEV" "devdax"
assert_daxmode_6.19 "$DEV" "devdax" "test_daxmode: pre-condition devdax for logplay test"

expect_good "${CLI[@]}" logplay --shadow "$SHADOWPATH" \
    --daxdev "$DEV" --set-daxmode -vv \
    -- "test_daxmode: logplay --shadow --set-daxmode should switch to famfs and replay"
assert_daxmode_6.19 "$DEV" "famfs" "test_daxmode: device should be famfs after logplay --set-daxmode"

#
# Test 4: shadow logplay --client (exercises the client_mode override in
# famfs_dax_shadow_logplay() on the dummy-mount path).
#
# In shadow mode __famfs_logplay() creates YAML metadata files regardless of
# role, so both master and client shadow logplay produce the same output.
# The goal here is to exercise the --client code path (previously untested
# on the dummy-mount path) and verify it does not crash or fail.
#
echo ""
echo ":=== test_daxmode: TEST 4: shadow logplay --client (dummy-mount path) ==="
assert_daxmode_6.19 "$DEV" "famfs" "test_daxmode: pre-condition famfs for client logplay test"

expect_good "${CLI[@]}" logplay --shadow "$SHADOWPATH" --daxdev "$DEV" -vv \
    -- "test_daxmode: shadow logplay (master role) should succeed on dummy-mount path"

expect_good "${CLI[@]}" logplay --shadow "$SHADOWPATH" --daxdev "$DEV" --client -vv \
    -- "test_daxmode: shadow logplay --client (client role override) should succeed"

#
# Negative tests: commands without --set-daxmode should fail when device is in devdax mode.
# One daxctl switch sets up all three checks.
#
echo ""
echo ":=== test_daxmode: NEGATIVE TESTS: commands without --set-daxmode should fail ==="
dax_reconfigure_mode "$DEV" "devdax"
assert_daxmode_6.19 "$DEV" "devdax" "test_daxmode: pre-condition devdax for negative tests"

expect_fail "${MKFS[@]}" -f "$DEV" \
    -- "test_daxmode: mkfs without --set-daxmode should fail in devdax mode"
expect_fail "${MOUNT[@]}" "$DEV" "$MPT" \
    -- "test_daxmode: mount without --set-daxmode should fail in devdax mode"
expect_fail "${FSCK[@]}" "$DEV" \
    -- "test_daxmode: fsck without --set-daxmode should fail in devdax mode"

# Device is still in devdax after the failed commands; restore for subsequent tests.
dax_reconfigure_mode "$DEV" "famfs"
assert_daxmode_6.19 "$DEV" "famfs" "test_daxmode: restore famfs mode after negative tests"

# Rebuild a clean filesystem so subsequent smoke tests start from a known state.
expect_good "${MKFS[@]}" -f "$DEV" -- "test_daxmode: fresh mkfs after negative tests"
verify_dev_not_mounted "$DEV" "test_daxmode: dummy mount lingering after final mkfs?"

echo ":==*************************************************************************"
echo ":==test_daxmode completed successfully"
echo ":==*************************************************************************"
finish_test $TEST
exit 0
