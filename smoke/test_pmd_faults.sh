#!/usr/bin/env bash

TEST="test_pmd_faults"

source smoke/test_header.sh

source "$SCRIPTS/test_funcs.sh"

start_test $TEST
#set -x

# Verify that mmaps of famfs files are populated by PMD (2 MiB) faults, not
# PTE (4 KiB) faults. A kernel or fuse-server regression that leaves extents
# or interleave chunks 2 MiB-unaligned does not corrupt data; it just makes
# every 2 MiB region take 512 faults instead of one, and nothing else in the
# smoke tests would notice. test/pmd_fault_check.c counts the faults; the
# test/pmd_fault_check.sh wrapper also checks the fs_dax tracepoints.

HELPER="$BIN/pmd_fault_check"
WRAP="$cwd/test/pmd_fault_check.sh"
[[ -x "$HELPER" ]] || fail "$HELPER is missing; rebuild (test/CMakeLists.txt)"
[[ -x "$WRAP" ]] || fail "$WRAP is missing"

# The wrapper re-execs under sudo for tracefs; -b tells it where the helper is.
CHECK=(sudo "$WRAP" -b "$BIN")

# Start with a clean, empty, mounted famfs
famfs_recreate "pmd_faults"

# Interleaved files need the fuse ABIs or standalone KABI >= 43
INTERLEAVE_OK=1
if [[ "$FAMFS_MODE" == "v1" && "$FAMFS_ABI" -lt 43 ]]; then
    INTERLEAVE_OK=0
fi

# Bucket count for interleaved files, as in stripe_test.sh: 1 GiB buckets up
# to 16 GiB of capacity, 16 buckets beyond that. nstrips must be <= nbuckets.
CAPACITY=$(famfs_get_capacity "$MPT")
(( V16G = 16 * 1024 * 1024 * 1024 ))
if (( CAPACITY <= V16G )); then
    (( NBUCKETS = CAPACITY / (1024 * 1024 * 1024) ))
else
    (( NBUCKETS = 16 ))
fi
NSTRIPS=4
if (( NBUCKETS < NSTRIPS )); then
    NSTRIPS=$NBUCKETS
fi

#
# 1. Simple (non-interleaved) file, 64 MiB: 32 PMD faults expected, for both
#    read and write touches.
#
F="$MPT/pmd_simple"
expect_good "${CLI[@]}" "${CREAT[@]}" -s 64m "$F" \
           -- "creat 64 MiB simple file"
expect_good "${CHECK[@]}" -e pmd "$F" \
           -- "simple file: read faults are PMD"
expect_good "${CHECK[@]}" -e pmd -w "$F" \
           -- "simple file: write faults are PMD"

#
# 2. Negative control on the same file: a mapping whose 2 MiB colour does not
#    match the file offset must fall back to PTEs everywhere. This proves the
#    test can fail (and that the trace check sees the PTE faults).
#
expect_good "${CHECK[@]}" -e pte -m "$F" \
           -- "misaligned mapping falls back to PTE faults (negative control)"
expect_fail "${CHECK[@]}" -e pmd -m "$F" \
           -- "misaligned mapping is reported as not-PMD"

#
# 3. File whose size is not a 2 MiB multiple: 64 MiB + 4 KiB. The 32 full
#    PMDs must still be PMD faults; only the one tail page may be a PTE fault
#    (the helper predicts that from the file size).
#
F="$MPT/pmd_tail"
expect_good "${CLI[@]}" "${CREAT[@]}" -s 0x4001000 "$F" \
           -- "creat 64 MiB + 4 KiB simple file"
expect_good "${CHECK[@]}" -e pmd "$F" \
           -- "odd-sized file: full PMDs are PMD faults"

#
# 4. Interleaved files. Every strip base and every chunk boundary must be
#    2 MiB aligned on the device; a chunk size of 2 MiB is the hardest case
#    because every PMD is a separate chunk on a separate strip.
#
# Chunk sizes to try. 2 MiB is one PMD per chunk; 4 MiB puts two PMDs in each
# chunk. The standalone KABI 44 kernel accepts only chunk sizes equal to an
# allocation unit (4 KiB or 2 MiB; MAP_CREATE returns EINVAL otherwise), so
# the 4 MiB case is fuse-only there. KABI 43 and the fuse kernels take any
# power-of-2 multiple of the allocation unit.
CHUNKS="2m 4m"
if [[ "$FAMFS_MODE" == "v1" && "$FAMFS_ABI" -ge 44 ]]; then
    echo "Skipping the 4m chunk case: standalone KABI $FAMFS_ABI allows only 4k/2m chunks"
    CHUNKS="2m"
fi

if (( INTERLEAVE_OK )); then
    for CHUNK in $CHUNKS; do
        F="$MPT/pmd_interleaved_$CHUNK"
        expect_good "${CLI[@]}" "${CREAT[@]}" \
            -N "$NSTRIPS" -B "$NBUCKETS" -C "$CHUNK" -s 64m "$F" \
            -- "creat 64 MiB interleaved file (nstrips=$NSTRIPS chunk=$CHUNK)"
        expect_good "${CHECK[@]}" -e pmd "$F" \
            -- "interleaved chunk=$CHUNK: read faults are PMD"
        expect_good "${CHECK[@]}" -e pmd -w "$F" \
            -- "interleaved chunk=$CHUNK: write faults are PMD"
    done

    # Odd-sized interleaved file: 64 MiB + 4 KiB with 2 MiB chunks
    F="$MPT/pmd_interleaved_tail"
    expect_good "${CLI[@]}" "${CREAT[@]}" \
        -N "$NSTRIPS" -B "$NBUCKETS" -C 2m -s 0x4001000 "$F" \
        -- "creat 64 MiB + 4 KiB interleaved file"
    expect_good "${CHECK[@]}" -e pmd "$F" \
        -- "odd-sized interleaved file: full PMDs are PMD faults"
else
    echo "Skipping interleaved PMD fault checks (no interleave support in this mode/ABI)"
fi

set +x
finish_test $TEST
exit 0
