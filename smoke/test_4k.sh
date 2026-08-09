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
