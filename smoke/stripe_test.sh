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
    else
        MOUNT_OPTS="--nofuse" # Can drop this b/c fuse is the default
    fi
else
    echo "FAMFS_MODE: invalid"
    exit 1;
fi

MOUNT="sudo $VG $BIN/famfs mount $MOUNT_OPTS"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
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
	${CLI} creat  -C "$CHUNKSIZE" -N "$NSTRIPS" -B "$NBUCKETS" -s "$SIZE" "$file_name"
	# Assert if file creation failed
	assert_equal $? 0 "Failed to create interleaved file $file_name"

	dst_name="$file_name""_copy"
	echo "Copying $file_name into $dst_name"
	${CLI} cp  -C "$CHUNKSIZE" -N "$NSTRIPS" -B "$NBUCKETS" "$file_name" "$dst_name" || fail "striped file cp of $dst_name failed"

	# Add the file name to the array
	files+=("$dst_name")
	# Increment the counter
	((counter++))
    done

    loopct=0
    randomize_args=()
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	#	echo -n "Randomizing file: $file seed=$seed"
	randomize_args+=("-M")
	randomize_args+=("${file},${SIZE},${seed} ")
	(( loopct++ ))
    done
    ${CLI} creat ${randomize_args[@]}
    rc=$?
    if [[ $rc -eq 0 ]]; then
	echo "...done"
    else
	fail "$rc initialization failures"
    fi

    # TODO: if the the FAMFS_KABI_VERSION >= 43, verify that the files are striped

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
    ${CLI} verify ${verify_args[@]}
    rc="$?"
    if [[ $rc -eq 0 ]]; then
	echo "...good"
    else
	fail "Failed to verify $rc files (seed=$seed)"
    fi
    echo "rc=$rc"
    
    echo "Created and copied $counter files"
    echo "Processed all successfully copied files."
}

stripe_test () {
    SIZE=$1
    CHUNKSIZE=$2
    NSTRIPS=$3
    NBUCKETS=$4
    BASE_SEED=$5
    counter=0;

    files=()

    # Create files in a loop until file creation fails
    while true; do
	# Generate a file name
	file_name=$(printf "${BASENAME}_%05d" "$counter")
	#file_name="${BASENAME}_${counter}"

	echo "Creating file $counter:$file_name"
	# Try to create the file
	${CLI} creat  -C "$CHUNKSIZE" -N "$NSTRIPS" -B "$NBUCKETS" -s "$SIZE" "$file_name"
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
    done

    echo "created $counter files"

    #
    # Randomize the files and remember the seeds
    #
    loopct=0
    randomize_args=()
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))

	randomize_args+=("-M")
	randomize_args+=("${file},${SIZE},${seed} ")

	(( loopct++ ))
    done
    #set -x
    ${CLI} creat ${randomize_args[@]}
    rc=$?
    if [[ $rc -eq 0 ]]; then
	echo "...done"
    else
	fail "$rc failures from initialization"
    fi

    # TODO: if the the FAMFS_KABI_VERSION >= 43, verify that the files are striped

    #
    # Check the files with the "remembered" seeds
    #
    echo "verifying files"
    # Cat each file to /dev/null
    loopct=0
    verify_args=()
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo -n "verifying file: $file seed=$seed"
	verify_args+=("-m")
	verify_args+=("${file},${seed} ")
	
	(( loopct++ ))
    done
    ${CLI} verify ${verify_args[@]}
    rc="$?"
    if [[ $rc -eq 0 ]]; then
	echo "...good"
    else
	fail "Failed to verify $rc files (seed=$seed)"
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
	echo -n "re-verifying file: $file seed=$seed"
	verify_args+=("-m")
	verify_args+=("${file},${seed} ")

	(( loopct++ ))
    done

    ${CLI} verify ${verify_args[@]}
    rc="$?"
    if [[ $rc -eq 0 ]]; then
	echo "...good"
    else
	fail "Failed to verify $rc files (seed=$seed)"
    fi

    echo "Processed all successfully created files."
}

#set -x

# Start with a clean, empty file systeem
famfs_recreate "stripe_test.sh"

BASENAME="/mnt/famfs/stripe_file"
CAPACITY=$(famfs_get_capacity "$MPT")
echo "Capacity: $CAPACITY"
(( NBUCKETS = CAPACITY / (1024 * 1024 * 1024) ))
(( NSTRIPS = NBUCKETS - 1 ))
echo "NBUCKETS: $NBUCKETS"
echo "NSTRIPS: $NSTRIPS"

(( SIZE = 256 * 1048576 ))
(( CHUNKSIZE= 2 * 1048576 ))

stripe_test "$SIZE" "$CHUNKSIZE" "$NSTRIPS" "$NBUCKETS" 42

famfs_recreate "stripe_test.sh 2"
(( NFILES = 8 ))
stripe_test_cp "$SIZE" "$CHUNKSIZE" "$NSTRIPS" "$NBUCKETS" 42 "$NFILES"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/stripe_test.shadow $MPT

# TODO: print some stats on how much stranded space remained

set +x
echo "*************************************************************************"
echo "stripe_test completed successfully"
echo "*************************************************************************"
exit 0
