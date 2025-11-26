#!/usr/bin/env bash

set -euo pipefail

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

TEST="test_fio"
start_test $TEST

echo "SCRIPTS=$SCRIPTS $(realpath "$SCRIPTS")"
REALSCRIPTS=$(realpath "$SCRIPTS")

# Start with a clean, empty file system
famfs_recreate "test_fio"

verify_mounted "$DEV" "$MPT" "test_fio.sh"

TESTDIR="$MPT/fio_stress"

expect_good "${CLI[@]}" mkdir -p "$TESTDIR" \
           -- "mkdir $TESTDIR should succeed"

#
# Extract free space — this was previously broken
#
# Correct usage:
#   famfs fsck <path> | grep "Free space" | awk '{print $3}'
#
SPACE_AVAIL=$(
    "${CLI[@]}" fsck "$TESTDIR" \
    | grep "Free space" \
    | awk '{print $3}'
)

echo "SPACE_AVAIL=$SPACE_AVAIL"

# Not a stress test, just a smoke test (4 jobs)
expect_good "$REALSCRIPTS/stress_fio.sh" \
           -v \
           -b "$BIN" \
           -r 30 \
           -s "$SPACE_AVAIL" \
           -p "$TESTDIR" \
           -j 4 \
           -- "test_fio failed"

#
# Shadow logplay of metadata
#
SHADOW="/tmp/smoke.shadow/test_fio.shadow"

expect_good sudo mkdir -p "$SHADOW/root" \
           -- "mkdir $SHADOW should work"

expect_good "${CLI[@]}" logplay -sS "$SHADOW" "$MPT" \
           -- "shadow logplay should work"

set +x
finish_test $TEST
exit 0
