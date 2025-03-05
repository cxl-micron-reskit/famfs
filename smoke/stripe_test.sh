#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/debug"
SCRIPTS="$CWD/scripts"

# Allow these variables to be set from the environment
if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$DEV" ]; then
    DEV="/dev/dax0.0"
fi

# Check if we have password-less sudo, which is required
sudo -n true 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: password-less sudo capability is required to run stress tests"
fi

TEST_FUNCS=$SCRIPTS/test_funcs.sh
if [ ! -f $TEST_FUNCS ]; then
    echo "Can't source $TEST_FUNCS"
    exit -1
fi

while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
        (-d|--device)
            DEV=$1
            shift;
            ;;
	(-b|--bin)
	    BIN=$1
	    shift
	    ;;
	(-m|--mpt)
	    MPT=$1
	    shift;
	    ;;
	(-s|--scripts)
	    SCRIPTS=$1
	    source_root=$1;
	    shift;
	    ;;
	(-v|--valgrind)
	    # no argument to -v; just setup for Valgrind
	    VG=${VALGRIND_ARG}
	    ;;
        *)
            echo "Unrecognized command line arg: $flag"
	    exit -1
            ;;
    esac
done

CLI="sudo $VG $BIN/famfs"

echo "CWD:      $CWD"
echo "BIN:      $BIN"
echo "SCRIPTS:  $SCRIPTS"
echo "CLI: 	$CLI"

if [ ! -d $BIN ]; then
    echo "Can't find executables"
    exit -1
fi
if [ ! -x "$BIN/famfs" ]; then
    echo "famfs cli missing or not built in subdir $BIN"
    exit -1
fi

source $TEST_FUNCS

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
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo -n "Randomizing file: $file seed=$seed"
	${CLI} creat  -r -S "$seed" -s "$SIZE" "$file"
	if [[ $? -eq 0 ]]; then
	    echo "...done"
	else
	    fail "Failed to initialize $file (seed=$seed)"
	fi
	(( loopct++ ))
    done

    # TODO: if the the FAMFS_KABI_VERSION >= 43, verify that the files are striped

    #
    # Check the files with the "remembered" seeds
    #
    echo "Verifying files"
    loopct=0
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo -n "Verifying file: $file seed=$seed"
	${CLI} verify -q -S "$seed" -f "$file"
	if [[ $? -eq 0 ]]; then
	    echo "...good"
	else
	    fail "Failed to verify $file (seed=$seed)"
	fi
	(( loopct++ ))
    done

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
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo -n "Randomizing file: $file seed=$seed"
	${CLI} creat  -r -S "$seed" -s "$SIZE" "$file"
	if [[ $? -eq 0 ]]; then
	    echo "...done"
	else
	    fail "Failed to initialize $file (seed=$seed)"
	fi
	(( loopct++ ))
    done

    # TODO: if the the FAMFS_KABI_VERSION >= 43, verify that the files are striped

    #
    # Check the files with the "remembered" seeds
    #
    echo "Verifying files"
    # Cat each file to /dev/null
    loopct=0
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo -n "Verifying file: $file seed=$seed"
	${CLI} verify -q -S "$seed" -f "$file"
	if [[ $? -eq 0 ]]; then
	    echo "...good"
	else
	    fail "Failed to verify $file (seed=$seed)"
	fi
	(( loopct++ ))
    done

    echo
    echo "Unmount and remount to test logplay for interleaved files"
    echo
    sudo umount $MPT || fail "umount should work"
    verify_not_mounted $DEV $MPT "stripe_test umount should have succeeded"
    ${CLI} mount $DEV $MPT  || fail "remount should work"
    verify_mounted $DEV $MPT "stripe_test remount should have succeeded"

    #
    # Re-check the files after unmount/remount to verify metadata log handling of
    # interleaved files
    #
    echo "Verifying files post unmount/remount"
    # Cat each file to /dev/null
    loopct=0
    for file in "${files[@]}"; do
	(( seed = BASE_SEED + loopct ))
	echo -n "re-verifying file: $file seed=$seed"
	${CLI} verify -q -S "$seed" -f "$file"
	if [[ $? -eq 0 ]]; then
	    echo "...good"
	else
	    fail "Failed to verify $file after unmount/remount (seed=$seed)"
	fi
	(( loopct++ ))
    done

    echo "Processed all successfully created files."
}

#set -x

# Start with a clean, empty file systeem
famfs_recreate -d "$DEV" -b "$BIN" -m "$MPT" -M "recreate in stripe_test.sh"

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

famfs_recreate -d "$DEV" -b "$BIN"
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
