#!/usr/bin/env bash

TEST="mmap"

source smoke/test_header.sh

source "$SCRIPTS/test_funcs.sh"

start_test $TEST
#set -x

# Start with a clean, empty, mounted famfs
famfs_recreate "mmap"

VERIFY="$(dirname "$0")/mmap_shared_private.py"

# A 4 MiB seed-initialized file: large enough for a 2 MiB (PMD) mapping at a
# 2 MiB-aligned offset. -r/-S gives it known, nonzero content.
F="$MPT/mmap_test"
expect_good "${CLI[@]}" creat -r -s 0x400000 -S 7 "$F" \
           -- "creat mmap test file"

# PTE (4 KiB) region at offset 0:
#   shared mmap must write through to FAM; private mmap must be copy-on-write.
expect_good sudo python3 "$VERIFY" "$F" 0 0x1000 \
           -- "mmap PTE: shared writes through, private is COW"

# PMD (2 MiB) region at a 2 MiB-aligned offset: same guarantees, exercising the
# huge-fault path (private 2 MiB writes fall back to PTE COW in the DAX core).
expect_good sudo python3 "$VERIFY" "$F" 0x200000 0x200000 \
           -- "mmap PMD: shared writes through, private is COW"

set +x
finish_test $TEST
exit 0
