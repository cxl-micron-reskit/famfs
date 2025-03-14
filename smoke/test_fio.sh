#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=../scripts/
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
RMMOD=0

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

# Override defaults as needed
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
	(-s|--scripts)
	    SCRIPTS=$1
	    source_root=$1;
	    shift;
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

echo "DEVTYPE=$DEVTYPE"
echo "SCRIPTS=$SCRIPTS $(realpath $SCRIPTS)"
REALSCRIPTS=$(realpath $SCRIPTS)
CLI="sudo $VG $BIN/famfs"
TEST="test_fio"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Start with a clean, empty file systeem
famfs_recreate "test_fio"

verify_mounted $DEV $MPT "test_fio.sh"

TESTDIR="$MPT/fio_stress"
${CLI} mkdir -p $TESTDIR

SPACE_AVAIL=$(sudo $BIN/famfs fsck $TESTDIR | grep "Free space" | awk -e '{print $3}')

pwd
# Not a stress test, just a smoke test (4 jobs)
$REALSCRIPTS/stress_fio.sh \
		       -b $BIN \
		       -r 30 \
		       -s $SPACE_AVAIL \
		       -p $TESTDIR \
		       -j 4 || fail "test_fio failed"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_fio.shadow $MPT

set +x
echo "*************************************************************************"
echo "$TEST completed successfully"
echo "*************************************************************************"
exit 0
