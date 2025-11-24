#!/usr/bin/env bash

source smoke/test_header.sh

TEST="stripe_test"

source $SCRIPTS/test_funcs.sh

CREAT="creat -t 1"

stripe_test_cp () {
    SIZE=$1
    CHUNKSIZE=$2
    NSTRIPS=$3
    NBUCKETS=$4
    BASE_SEED=$5
    counter=0;
    NFILES=$6

    files=()

    # Create files in a loop
    while [ $counter -lt $NFILES ]; do
	# Generate a file name
	file_name=$(printf "${BASENAME}_%05d" "$counter")

	echo "stripe_test_cp: Creating file $counter:$file_name"
	# Try to create the file
	${CLI} ${CREAT}  -N "$NSTRIPS" -B "$NBUCKETS" -s "$SIZE" "$file_name"
	# Assert if file creation failed
	assert_equal $? 0 "Failed to create interleaved file $file_name"

	# If we're not using valgrind, generate a random thread count
	if [[ -z "${VG//[[:space:]]/}" ]]; then
	    THREADCT=$(generate_random_int "1" "96")
	    CP_ARGS="-t $THREADCT"
	fi

	dst_name="$file_name""_copy"
	echo "Copying $file_name into $dst_name ($CP_ARGS)"
	${CLI} ${CP} ${CP_ARGS} -N "$NSTRIPS" -B "$NBUCKETS" "$file_name" "$dst_name" || fail_fsck "striped file cp of $dst_name failed" "$B $NBUCKETS"

	# Add the file name to the array
	files+=("$dst_name")
	# Increment the counter
	((counter++))
    done

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches

    loopct=0
    randomize_args=()
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	#	echo -n "Randomizing file: $file seed=$seed"
	randomize_args+=("-M")
	randomize_args+=("${file},${SIZE},${seed} ")
	(( loopct++ ))
    done
    echo "performing threaded randomize on ${loopct} files:"
    ${CLI} ${CREAT} ${randomize_args[@]}
    rc=$?
    if [[ $rc -eq 0 ]]; then
	echo "...done"
    else
	fail_fsck "$rc initialization failures" "-B $NBUCKETS"
    fi

    # TODO: if the the FAMFS_KABI_VERSION >= 43, verify that the files are striped

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches

    #
    # Check the files with the "remembered" seeds
    #
    verify_args=()
    echo "Verifying files"
    loopct=0
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	#echo -n "Verifying file: $file seed=$seed"
	verify_args+=("-m")
	verify_args+=("${file},${seed} ")

	(( loopct++ ))
    done
    ${CLI} ${VERIFY} ${verify_args[@]}
    rc="$?"
    if [[ $rc -eq 0 ]]; then
	echo "...good"
    else
	fail_fsck "Failed to verify $rc files (seed=$seed)" "-B $NBUCKETS"
    fi
    echo "rc=$rc"
    
    ${FSCK} -h -B ${NBUCKETS} ${MPT}
    ${FSCK} -h -B 1 ${MPT}
    ${FSCK} -B ${NBUCKETS} ${MPT}
    ${FSCK} -B $(( NBUCKETS - 1 )) ${MPT}
    ${FSCK} -B $(( NBUCKETS - 2 )) ${MPT}
    echo "Created and copied $counter files"
    echo "Processed all successfully copied files."
}

stripe_test () {
    SIZE_MIN=$1
    SIZE_MAX=$2
    CHUNKSIZE=$3
    NSTRIPS=$4
    NBUCKETS=$5
    BASE_SEED=$6
    counter=0;

    files=()

    # Create files in a loop until file creation fails
    while true; do
	# Generate a file name
	file_name=$(printf "${BASENAME}_%05d" "$counter")
	#file_name="${BASENAME}_${counter}"

	SIZE=$(generate_random_int "$SIZE_MIN" "$SIZE_MAX")
	echo "SIZE $SIZE"

	echo "stripe_test: Creating file $counter:$file_name"
	# Try to create the file
	set +e
	${CLI} ${CREAT}  -C "$CHUNKSIZE" -N "$NSTRIPS" -B "$NBUCKETS" \
	       -s "$SIZE" "$file_name"
	RC="$?"
	set -e
	if [[ "$RC" -ne 0 ]]; then
	    echo "File creation failed on $file_name"
	    # Assert if counter is 0 i.e. first interleaved file creation failed
	    assert_ne $counter 0 "Failed to create any interleaved files"
	    break
	fi

	#break;
	# Add the file name to the array
	files+=("$file_name")

	# Increment the counter
	((counter++))

	# Our smoke test VMs have 8GiB virtual daxdevs. On an 8G daxdev,
	# this will create test files until it is full. But if the daxdev
	# is huge, we don't want this test to take forever, so bail at 26
	# files (which is the number you get on an 8G daxdev).
	if [ "$counter" -gt 26 ]; then
	    echo ":== stripe_test: Not filling large file system"
	    break;
	fi
    done

    echo "created $counter files"

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches

    #
    # Randomize the files and remember the seeds
    #
    loopct=0
    randomize_args=()
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))

	randomize_args+=("-M")
	randomize_args+=("${file},0,${seed} ")

	(( loopct++ ))
    done
    #set -x
    set +e
    ${CLI} ${CREAT} -t0 -v ${randomize_args[@]}
    RC="$?"
    set -e
    if [[ "$RC" -eq 0 ]]; then
	echo "...done"
    else
	fail_fsck "$RC failures from initialization" "-B $NBUCKETS"
    fi

    # TODO: if the the FAMFS_KABI_VERSION >= 43, verify that the files are striped

    # Cause some eviction and re-reading
    echo 2 | sudo tee /proc/sys/vm/drop_caches

    #
    # Check the files with the "remembered" seeds
    #
    echo "verifying files"
    # Cat each file to /dev/null
    loopct=0
    verify_args=()
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo "verifying file: $file seed=$seed"
	verify_args+=("-m")
	verify_args+=("${file},${seed} ")
	
	(( loopct++ ))
    done
    set +e
    ${CLI} ${VERIFY} ${verify_args[@]}
    RC="$?"
    set -e
    if [[ "$RC" -eq 0 ]]; then
	echo "...good"
    else
	echo "Failed to verify $RC files (seed=$seed) ***********************"
	${FSCK} -hv $MPT
	fail "Failed to verify $rc files (seed=$seed) ***********************" "-B $NBUCKETS"
    fi

    # Dump icache stats before umount
    if [[ "$FAMFS_MODE" == "fuse" ]]; then
	# turn up log debug
	expect_good sudo curl  \
		    --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
		    http://localhost/icache_stats -- "rest fail"
    fi

    echo
    echo "Unmount and remount to test logplay for interleaved files"
    echo
    expect_good sudo umount $MPT -- "umount should work"
    verify_not_mounted $DEV $MPT "stripe_test umount should have succeeded"
    expect_good ${MOUNT} $DEV $MPT  -- "remount should work"
    verify_mounted $DEV $MPT "stripe_test remount should have succeeded"

    #
    # Re-check the files after unmount/remount to verify metadata log handling of
    # interleaved files
    #
    echo "Verifying files post unmount/remount"
    # Cat each file to /dev/null
    loopct=0
    verify_args=()
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo "re-verifying file: $file seed=$seed"
	verify_args+=("-m")
	verify_args+=("${file},${seed} ")

	(( loopct++ ))
    done

    set +e
    ${CLI} ${VERIFY} -v ${verify_args[@]}
    RC="$?"
    set -e
    if [[ "$RC" -eq 0 ]]; then
	echo "...good"
    else
	fail "Failed to verify $RC files (seed=$seed)"
    fi

    expect_good ${FSCK} -h -B ${NBUCKETS} ${MPT} -- "fsck should work"
    echo "Processed all successfully created files."
}

#set -x

# Start with a clean, empty file systeem
famfs_recreate "stripe_test.sh"

expect_fail ${CLI} ${CREAT} -vv -B 100 -C 2m -N 3 -s 2m $MPT/toomanybuckets \
	    -- "too many buckets should fail"
expect_fail ${CLI} ${CREAT} -B 10 -C 2000000 -N 3 -s 2m $MPT/badchunksz \
	    -- "bad chunk size should fail"

#set -x
BASENAME="/mnt/famfs/stripe_file"
CAPACITY=$(famfs_get_capacity "$MPT")
echo "Capacity: $CAPACITY"

# Bucket size is 1G and NBUCKETS is calculated if the CAPACITY <=16G. Otherwise,
# NBUCKETS = 16 (and bucket size is CAPACITY/16). 
# Standalone famfs has a 16 strip limit that won't be fixed, but fuse based
# famfs has a 32 strip limit (and that can increase more easily - because the fmap
# message format in famfs-fuse is more flexible. 
(( V16G = 16 * 1024 * 1024 * 1024 ))
if [ "$CAPACITY" -le "$V16G" ]; then
    (( NBUCKETS = CAPACITY / (1024 * 1024 * 1024) ))
else
    (( NBUCKETS = 16 ))
fi

(( NSTRIPS = NBUCKETS - 1 ))
echo "NBUCKETS: $NBUCKETS"
echo "NSTRIPS: $NSTRIPS"

(( SIZE_MAX = 256 * 1048576 ))
(( SIZE_MIN = SIZE_MAX - (2 * 1048576) ))
(( CHUNKSIZE= 2 * 1048576 ))

#stripe_test "$SIZE_MIN" "$SIZE_MAX" "$CHUNKSIZE" "$NSTRIPS" "$NBUCKETS" 42

famfs_recreate "stripe_test.sh 2"
(( NFILES = 8 ))
(( SIZE = 256 * 1048576 ))

stripe_test_cp "$SIZE" "$CHUNKSIZE" "$NSTRIPS" "$NBUCKETS" 42 "$NFILES"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/stripe_test.shadow $MPT

# TODO: print some stats on how much stranded space remained

set +x
echo ":==*************************************************************************"
echo ":==stripe_test completed successfully"
echo ":==*************************************************************************"
exit 0
