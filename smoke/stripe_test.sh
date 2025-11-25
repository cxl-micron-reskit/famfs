#!/usr/bin/env bash

# Fail fast in the test script itself; helpers manage errexit internally.
set -e
set -o pipefail

source smoke/test_header.sh

TEST="stripe_test"

source "$SCRIPTS/test_funcs.sh"

# ---------------------------------------------------------------------
# stripe_test_cp:
#   Create NFILES striped files, copy them with random thread counts,
#   randomize them with seeds, then verify via multi-verify.
# ---------------------------------------------------------------------
stripe_test_cp () {
    local SIZE="$1"
    local CHUNKSIZE="$2"
    local NSTRIPS="$3"
    local NBUCKETS="$4"
    local BASE_SEED="$5"
    local NFILES="$6"

    local counter=0
    local -a files=()

    # Don't run in set -e mode in this function
    set +e

    # Create files in a loop
    while (( counter < NFILES )); do
        local file_name
        file_name=$(printf "%s_%05d" "${BASENAME}" "$counter")

        echo "stripe_test_cp: Creating file $counter:$file_name"

        # Create the striped file
        expect_good \
            "${CLI[@]}" "${CREAT[@]}" \
            -N "$NSTRIPS" -B "$NBUCKETS" -s "$SIZE" "$file_name" \
            -- "Failed to create interleaved file $file_name"

        # If we're not using valgrind, generate a random thread count for cp
        local -a cp_args=()
        if [[ -z "${VG//[[:space:]]/}" ]]; then
            local THREADCT
            THREADCT=$(generate_random_int "1" "96")
            cp_args+=("-t" "$THREADCT")
        fi

        local dst_name="${file_name}_copy"
        echo "Copying $file_name into $dst_name (${cp_args[*]})"

        "${CLI[@]}" "${CP[@]}" "${cp_args[@]}" \
            -N "$NSTRIPS" -B "$NBUCKETS" "$file_name" "$dst_name" \
            || fail_fsck "striped file cp of $dst_name failed" "-B $NBUCKETS"

        files+=("$dst_name")
        ((counter++))
    done

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches >/dev/null

    #
    # Randomize the files with remembered seeds
    #
    local loopct=0
    local -a randomize_args=()

    for file in "${files[@]}"; do
        local seed=$(( BASE_SEED + loopct ))
        randomize_args+=("-M" "${file},${SIZE},${seed}")
        ((loopct++))
    done

    echo "performing threaded randomize on ${loopct} files:"

    local -a CMD=(
        "${CLI[@]}"
        "${CREAT[@]}"
        "${randomize_args[@]}"
    )
    echo "stripe_test_cp: ${CMD[*]}"
    "${CMD[@]}"
    local rc=$?

    if (( rc == 0 )); then
        echo "...done"
    else
        fail_fsck "$rc initialization failures" "-B $NBUCKETS"
    fi

    # TODO: if FAMFS_KABI_VERSION >= 43, verify that the files are striped

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches >/dev/null

    #
    # Verify the files with the remembered seeds
    #
    echo "Verifying files"
    loopct=0
    local -a verify_args=()

    for file in "${files[@]}"; do
        local seed=$(( BASE_SEED + loopct ))
        verify_args+=("-m" "${file},${seed}")
        ((loopct++))
    done

    CMD=(
        "${CLI[@]}"
        "${VERIFY[@]}"
        "${verify_args[@]}"
    )
    echo "stripe_test_cp: ${CMD[*]}"
    "${CMD[@]}"
    rc=$?

    if (( rc == 0 )); then
        echo "...good"
    else
        fail_fsck "Failed to verify $rc files (seed=$seed)" "-B $NBUCKETS"
    fi
    echo "rc=$rc"

    # Various fsck bucket-count tests
    "${FSCK[@]}" -h -B "${NBUCKETS}" "${MPT}"
    "${FSCK[@]}" -h -B 1 "${MPT}"
    "${FSCK[@]}" -B "${NBUCKETS}" "${MPT}"
    "${FSCK[@]}" -B $(( NBUCKETS - 1 )) "${MPT}"
    "${FSCK[@]}" -B $(( NBUCKETS - 2 )) "${MPT}"

    echo "Created and copied $counter files"
    echo "Processed all successfully copied files."

    # put set -e back in effect
    set -e 
}

# ---------------------------------------------------------------------
# stripe_test:
#   Fill with striped files until daxdev is full-ish or until 26 files.
#   Randomize and verify using seeds, then remount and re-verify.
# ---------------------------------------------------------------------
stripe_test () {
    local SIZE_MIN="$1"
    local SIZE_MAX="$2"
    local CHUNKSIZE="$3"
    local NSTRIPS="$4"
    local NBUCKETS="$5"
    local BASE_SEED="$6"

    local counter=0
    local -a files=()

    # Don't run in set -e mode in this function
    set +e

    #
    # Create files in a loop until creation fails or we hit max count.
    #
    while true; do
	echo "top $counter"
        local file_name
        file_name=$(printf "%s_%05d" "${BASENAME}" "$counter")

        local SIZE
        SIZE=$(generate_random_int "$SIZE_MIN" "$SIZE_MAX")
        echo "SIZE $SIZE"

        echo "stripe_test: Creating file $counter:$file_name"

    	local CMD=(
	    "${CLI[@]}"
	    "${CREAT[@]}"
	    "-C" "$CHUNKSIZE"
	    "-N" "$NSTRIPS"
	    "-B" "$NBUCKETS"
	    "-s" "$SIZE"
	    "$file_name"
	)
        echo "stripe_test: ${CMD[*]}"
        "${CMD[@]}"
        local RC=$?

	echo "Created $file_name, RC=$RC"
        if (( RC != 0 )); then
            echo "File creation failed on $file_name"
            # Assert if counter is 0 i.e. first interleaved file creation failed
            assert_ne "$counter" 0 "Failed to create any interleaved files"
            break
        fi

        files+=("$file_name")
        ((counter++))

        # On an 8G daxdev, this will fill the device. For larger devices,
        # don't let the test run forever: bail out at 26 files.
        if (( counter > 26 )); then
            echo ":== stripe_test: Not filling large file system"
            break
        fi
	echo "bottom $counter"
    done

    echo "created $counter files"

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches >/dev/null

    #
    # Randomize the files and remember the seeds
    #
    local loopct=0
    local -a randomize_args=()

    for file in "${files[@]}"; do
        local seed=$(( BASE_SEED + loopct ))
        randomize_args+=("-M" "${file},0,${seed}")
        ((loopct++))
    done

    CMD=(
        "${CLI[@]}"
        "${CREAT[@]}"
        "-t0"
        "-v"
        "${randomize_args[@]}"
    )
    echo "stripe_test: ${CMD[*]}"
    "${CMD[@]}"
    RC=$?

    if (( RC == 0 )); then
        echo "...done"
    else
        fail_fsck "$RC failures from initialization" "-B $NBUCKETS"
    fi

    # TODO: if FAMFS_KABI_VERSION >= 43, verify that the files are striped

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches >/dev/null

    #
    # Check the files with the remembered seeds
    #
    echo "verifying files"
    loopct=0
    local -a verify_args=()

    for file in "${files[@]}"; do
        local seed=$(( BASE_SEED + loopct ))
        echo "verifying file: $file seed=$seed"
        verify_args+=("-m" "${file},${seed}")
        ((loopct++))
    done

    CMD=(
        "${CLI[@]}"
        "${VERIFY[@]}"
        "${verify_args[@]}"
    )
    echo "stripe_test: ${CMD[*]}"
    "${CMD[@]}"
    RC=$?

    if (( RC == 0 )); then
        echo "...good"
    else
        echo "Failed to verify $RC files (seed=$seed) ***********************"
        "${FSCK[@]}" -hv "$MPT"
        fail "Failed to verify $RC files (seed=$seed) ***********************"
    fi

    # Dump icache stats before umount (fuse mode only)
    if [[ "$FAMFS_MODE" == "fuse" ]]; then
        expect_good \
            sudo curl \
                --unix-socket "$(scripts/famfs_shadow.sh "$MPT")/sock" \
                http://localhost/icache_stats \
            -- "rest fail"
    fi

    echo
    echo "Unmount and remount to test logplay for interleaved files"
    echo
    expect_good sudo "$UMOUNT" "$MPT" -- "umount should work"
    verify_not_mounted "$DEV" "$MPT" "stripe_test umount should have succeeded"
    expect_good "${MOUNT[@]}" "$DEV" "$MPT" -- "remount should work"
    verify_mounted "$DEV" "$MPT" "stripe_test remount should have succeeded"

    #
    # Re-check the files after unmount/remount to verify metadata log handling
    # of interleaved files.
    #
    echo "Verifying files post unmount/remount"
    loopct=0
    verify_args=()

    for file in "${files[@]}"; do
        local seed=$(( BASE_SEED + loopct ))
        echo "re-verifying file: $file seed=$seed"
        verify_args+=("-m" "${file},${seed}")
        ((loopct++))
    done

    CMD=(
        "${CLI[@]}"
        "${VERIFY[@]}"
        "-v"
        "${verify_args[@]}"
    )
    echo "stripe_test: ${CMD[*]}"
    "${CMD[@]}"
    RC=$?

    if (( RC == 0 )); then
        echo "...good"
    else
        fail "Failed to verify $RC files (seed=$seed)"
    fi

    expect_good "${FSCK[@]}" -h -B "${NBUCKETS}" "${MPT}" \
        -- "fsck should work"
    echo "Processed all successfully created files."

    # put set -e back in effect
    set -e 
}

# ---------------------------------------------------------------------
# Main body
# ---------------------------------------------------------------------

# Start with a clean, empty file system
famfs_recreate "stripe_test.sh"

# Basic bad-arg tests for creat with striping
expect_fail \
    "${CLI[@]}" "${CREAT[@]}" \
    -vv -B 100 -C 2m -N 3 -s 2m "$MPT/toomanybuckets" \
    -- "too many buckets should fail"

expect_fail \
    "${CLI[@]}" "${CREAT[@]}" \
    -B 10 -C 2000000 -N 3 -s 2m "$MPT/badchunksz" \
    -- "bad chunk size should fail"

BASENAME="$MPT/stripe_file"
CAPACITY=$(famfs_get_capacity "$MPT")
echo "Capacity: $CAPACITY"

# Bucket size is 1G and NBUCKETS is calculated if CAPACITY <=16G.
# Otherwise, NBUCKETS = 16 (bucket size = CAPACITY/16).
(( V16G = 16 * 1024 * 1024 * 1024 ))
if (( CAPACITY <= V16G )); then
    (( NBUCKETS = CAPACITY / (1024 * 1024 * 1024) ))
else
    (( NBUCKETS = 16 ))
fi

(( NSTRIPS = NBUCKETS - 1 ))
echo "NBUCKETS: $NBUCKETS"
echo "NSTRIPS: $NSTRIPS"

(( SIZE_MAX = 256 * 1048576 ))
(( SIZE_MIN = SIZE_MAX - (2 * 1048576) ))
(( CHUNKSIZE = 2 * 1048576 ))

expect_good df "$MPT" -- "df $MPT should work"

stripe_test "$SIZE_MIN" "$SIZE_MAX" "$CHUNKSIZE" "$NSTRIPS" "$NBUCKETS" 42

famfs_recreate "stripe_test.sh 2"
(( NFILES = 8 ))
(( SIZE = 256 * 1048576 ))

stripe_test_cp "$SIZE" "$CHUNKSIZE" "$NSTRIPS" "$NBUCKETS" 42 "$NFILES"

SHADOW="/tmp/smoke.shadow/stripe_test.shadow"
expect_good sudo mkdir -p $SHADOW/root
expect_good "${CLI[@]}" logplay -sS $SHADOW "$MPT" \
	    -- "shadow logplay should work for stripe_test"

set +x
echo ":==*************************************************************************"
echo ":==stripe_test completed successfully"
echo ":==*************************************************************************"
exit 0
