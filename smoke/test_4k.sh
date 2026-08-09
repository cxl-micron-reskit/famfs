#!/usr/bin/env bash

# Fail fast in the test script itself; helpers manage errexit internally.
set -e
set -o pipefail

TEST="test_4k"

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

start_test $TEST

# A 4KiB allocation unit is a standalone KABI>=44 feature (the kernel must
# accept 4K-aligned extents). run_smoke.sh gates this test, but guard here too
# in case it is run directly.
if [[ "$FAMFS_MODE" != "v1" || "$FAMFS_ABI" -lt 44 ]]; then
    echo ":== test_4k: skipped (needs standalone KABI >= 44; mode=$FAMFS_MODE abi=$FAMFS_ABI)"
    exit 0
fi

# A preceding test (e.g. stripe_test) may leave the fs mounted; start clean.
sudo umount "$MPT" >/dev/null 2>&1 || true
verify_not_mounted "$DEV" "$MPT" "test_4k start"

#
# mkfs with a 4K allocation unit; fsck must report it as 0x1000
#
expect_good "${MKFS[@]}" -f --4k "$DEV" -- "mkfs --4k"

out=$("${FSCK[@]}" "$DEV" 2>&1) || fail "test_4k: fsck failed after 'mkfs --4k'"
echo "$out" | grep -q "alloc_unit: *0x1000" \
    || fail "test_4k: expected 4K (0x1000) alloc unit; got: $(echo "$out" | grep -i alloc_unit)"
echo ":== test_4k: fsck reports 4K alloc unit after 'mkfs --4k'"

#
# Contrast: a default mkfs reports the 2MiB (0x200000) alloc unit
#
expect_good "${MKFS[@]}" -f "$DEV" -- "mkfs default (2M)"

out=$("${FSCK[@]}" "$DEV" 2>&1) || fail "test_4k: fsck failed after default mkfs"
echo "$out" | grep -q "alloc_unit: *0x200000" \
    || fail "test_4k: expected 2M (0x200000) alloc unit; got: $(echo "$out" | grep -i alloc_unit)"
echo ":== test_4k: fsck reports 2M alloc unit for default mkfs"

#
# End-to-end: mount a 4K filesystem, create a small file, and confirm the
# allocation is 4K-granular (not rounded up to 2MiB). famfs_recreate mkfs's
# with --4k and mounts.
#
famfs_recreate "test_4k e2e" --4k
verify_mounted "$DEV" "$MPT" "test_4k e2e"

# Allocated bytes before creating any file (superblock + log only)
base=$("${FSCK[@]}" "$MPT" 2>&1 | grep "Allocated bytes:" | awk '{print $3}') \
    || fail "test_4k: failed to read baseline 'Allocated bytes' from fsck"

# A 5000-byte file rounds up to 8KiB on a 4K fs (round_up(5000,4096)); a 2M
# alloc unit would consume 2MiB instead.
expect_good "${CLI[@]}" creat -r -S 1 -s 5000 "$MPT/small0" -- "creat small 4K file"
expect_good "${CLI[@]}" verify -S 1 -f "$MPT/small0"        -- "verify small 4K file"

after=$("${FSCK[@]}" "$MPT" 2>&1 | grep "Allocated bytes:" | awk '{print $3}') \
    || fail "test_4k: failed to read 'Allocated bytes' from fsck after creat"
delta=$((after - base))
echo ":== test_4k: 5000-byte file consumed $delta bytes (4K unit => 8192; 2M unit => 2097152)"
(( delta > 0 && delta <= 65536 )) \
    || fail "test_4k: small file consumed $delta bytes; expected <= 64KiB for 4K units"

expect_good "${FSCK[@]}" "$MPT" -- "fsck 4K filesystem should be clean"

sudo umount "$MPT" >/dev/null 2>&1 || true
