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
CLI="sudo $VG $BIN/famfs"
FSCK="${CLI} fsck $FSCK_OPTS"
TEST="test_fio"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests
echo "SCRIPTS=$SCRIPTS $(realpath $SCRIPTS)"
REALSCRIPTS=$(realpath $SCRIPTS)

set -x

# Start with a clean, empty file systeem
famfs_recreate "test_fio"

verify_mounted $DEV $MPT "test_fio.sh"

TESTDIR="$MPT/fio_stress"
${CLI} mkdir -p $TESTDIR

SPACE_AVAIL=$(sudo $BIN/famfs ${FSCK} $TESTDIR | grep "Free space" | awk -e '{print $3}')

pwd
# Not a stress test, just a smoke test (4 jobs)
expect_good $REALSCRIPTS/stress_fio.sh \
		       -v \
		       -b $BIN \
		       -r 30 \
		       -s $SPACE_AVAIL \
		       -p $TESTDIR \
		       -j 4 -- "test_fio failed"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_fio.shadow $MPT

set +x
echo ":==*************************************************************************"
echo ":==$TEST completed successfully"
echo ":==*************************************************************************"
exit 0
