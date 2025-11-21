#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=../scripts
RAW_MOUNT_OPTS="-t famfs -o noatime -o dax=always "
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
RMMOD=0
FAMFS_MOD="famfs.ko"

CREAT="creat"
VERIFY="verify"
CP="cp"

# Allow these variables to be set from the environment
if [ -z "$DEV" ]; then
    DEV="/dev/dax0.0"
fi
if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$UMOUNT" ]; then
    UMOUNT="umount"
fi
if [ -z "${FAMFS_MODE}" ]; then
    FAMFS_MODE="v1"
fi

# Override defaults as needed
while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
	(-M|--module)
	    FAMFS_MOD=$1
	    shift
	    ;;
	(-d|--device)
	    DEV=$1
	    shift;
	    ;;
	(-b|--bin)
	    BIN=$1
	    shift
	    ;;
	(-s|--scripts)
	    SCRIPTS=$1
	    source_root=$1;
	    shift;
	    ;;
	(-m|--mode)
	    FAMFS_MODE="$1"
	    shift
	    ;;
	(-v|--valgrind)
	    # no argument to -v; just setup for Valgrind
	    VG=${VALGRIND_ARG}
	    # Also: creat/cp multi threadct=1 to keep valgrind from crashing
	    CREAT="creat -t 1"
	    VERIFY="verify -t 1"
	    CP="cp -t 0"
	    ;;
	(-n|--no-rmmod)
	    RMMOD=0
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

if [[ "$FAMFS_MODE" == "v1" || "$FAMFS_MODE" == "fuse" ]]; then
    echo "FAMFS_MODE: $FAMFS_MODE"
    if [[ "$FAMFS_MODE" == "fuse" ]]; then
        MOUNT_OPTS="--fuse" # Can drop this b/c fuse is the default
	MKFS_OPTS="--nodax"
	FSCK_OPTS="--nodax"
    else
        MOUNT_OPTS="--nofuse" # Can drop this b/c fuse is the default
	MKFS_OPTS=""
	FSCK_OPTS=""
    fi
else
    echo "FAMFS_MODE: invalid"
    exit 1;
fi

MOUNT="sudo $VG $BIN/famfs mount $MOUNT_OPTS"
MKFS="sudo $VG $BIN/mkfs.famfs $MKFS_OPTS"
LI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
FSCK="${CLI} fsck $FSCK_OPTS"
TEST="stripe_test"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

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

	echo "Creating file $counter:$file_name"
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

	echo "Creating file $counter:$file_name"
	# Try to create the file
	${CLI} ${CREAT}  -C "$CHUNKSIZE" -N "$NSTRIPS" -B "$NBUCKETS" -s "$SIZE" "$file_name"
	if [[ $? -ne 0 ]]; then
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
    ${CLI} ${CREAT} -t0 -v ${randomize_args[@]}
    rc=$?
    if [[ $rc -eq 0 ]]; then
	echo "...done"
    else
	fail_fsck "$rc failures from initialization" "-B $NBUCKETS"
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
    ${CLI} ${VERIFY} ${verify_args[@]}
    rc="$?"
    if [[ $rc -eq 0 ]]; then
	echo "...good"
    else
	echo "Failed to verify $rc files (seed=$seed) ***********************"
	${FSCK} -hv $MPT
	fail_fsck "Failed to verify $rc files (seed=$seed) ***********************" "-B $NBUCKETS"
    fi

    # Dump icache stats before umount
    if [[ "$FAMFS_MODE" == "fuse" ]]; then
	# turn up log debug
	sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
	     http://localhost/icache_stats
    fi

    echo
    echo "Unmount and remount to test logplay for interleaved files"
    echo
    sudo umount $MPT || fail "umount should work"
    verify_not_mounted $DEV $MPT "stripe_test umount should have succeeded"
    ${MOUNT} $DEV $MPT  || fail "remount should work"
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

    ${CLI} ${VERIFY} -v ${verify_args[@]}
    rc="$?"
    if [[ $rc -eq 0 ]]; then
	echo "...good"
    else
	fail "Failed to verify $rc files (seed=$seed)"
    fi

    ${FSCK} -h -B ${NBUCKETS} ${MPT}
    echo "Processed all successfully created files."
}

#set -x

# Start with a clean, empty file systeem
famfs_recreate "stripe_test.sh"

${CLI} ${CREAT} -vv -B 100 -C 2m -N 3 -s 2m $MPT/toomanybuckets && fail "too many buckets should fail"
${CLI} ${CREAT} -B 10 -C 2000000 -N 3 -s 2m $MPT/badchunksz && fail "bad chunk size should fail"
 
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

stripe_test "$SIZE_MIN" "$SIZE_MAX" "$CHUNKSIZE" "$NSTRIPS" "$NBUCKETS" 42

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
