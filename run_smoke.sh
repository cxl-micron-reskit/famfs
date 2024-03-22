#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/debug"
SCRIPTS="$CWD/scripts"
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
TEST_ALL=1
SLEEP_TIME=2

# Allow these variables to be set from the environment
if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$DEV" ]; then
    DEV="/dev/pmem0"
fi
if [ -z "$ERRS" ]; then
    ERRS=1
fi

# Check if we have password-less sudi, which is required
sudo -n true 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: password-less sudo capability is required to run smoke tests"
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
	(-n|--noerrors)
	    ERRS=0
	    ;;
	(-j|--justerrors)
	    ERRS=1
	    TEST_ALL=0
	    ;;
	(-4)
	    TEST_ALL=0
	    TEST4=1
	    ;;
	(-N|--nosleep)
	    SLEEP_TIME=0
	    ;;
	(-r|--release)
	    BIN="$CWD/release"
	    echo "hello release BIN=$BIN"
	    ;;
	(-c|--coverage)
	    BIN="$CWD/coverage"
	    echo "hello coverage BIN=$BIN"
	    ;;
	(-v|--valgrind)
	    echo "run_smoke: valgrind mode"
	    VGARG="--valgrind"
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;
    esac
done

CLI="sudo $VG $BIN/famfs"

echo "CWD:      $CWD"
echo "BIN:      $BIN"
echo "SCRIPTS:  $SCRIPTS"
echo "ERRS:     $ERRS"

#BIN=debug
if [ ! -d $BIN ]; then
    echo "Can't find executables"
    exit -1
fi
if [ ! -x "$BIN/famfs" ]; then
    echo "famfs cli missing or not built in subdir $BIN"
    exit -1
fi

source $TEST_FUNCS

scripts/chk_memdev.sh "$DEV" || fail "Bad memory device $DEV"

# Verify that this script is not running as root
if [[ $EUID -eq 0 ]]; then
    echo "ERROR:"
    echo "You have run the smoke tests as root"
    echo "This does not work, as some of the tests assume they are running without root privileges"
    exit -1
else
    echo "Not running as root."
fi

# Verify that we are running with passwordless sudo, because that is a requirement
if sudo -l &>/dev/null; then
    echo "user has sudo privileges"
else
    echo "Error: this script requires sudo privileges"
    exit -1
fi

if (($TEST_ALL > 0)); then
    TEST0=1
    TEST1=1
    TEST2=1
    TEST3=1
    TEST4=1
    TEST_PCQ=1
fi

./smoke/prepare.sh $VGARG -b $BIN -s $SCRIPTS -d $DEV  || exit -1

if (($TEST0 > 0)); then
    ./smoke/test0.sh $VGARG -b $BIN -s $SCRIPTS -d $DEV  || exit -1
    sleep "${SLEEP_TIME}"
fi
if (($TEST1 > 0)); then
    ./smoke/test1.sh $VGARG -b $BIN -s $SCRIPTS -d $DEV  || exit -1
    sleep "${SLEEP_TIME}"
fi
if (($TEST2 > 0)); then
    ./smoke/test2.sh $VGARG -b $BIN -s $SCRIPTS -d $DEV  || exit -1
    sleep "${SLEEP_TIME}"
fi
if (($TEST3 > 0)); then
    ./smoke/test3.sh $VGARG -b $BIN -s $SCRIPTS -d $DEV  || exit -1
    sleep "${SLEEP_TIME}"
fi
if (($TEST4 > 0)); then
    ./smoke/test4.sh $VGARG -b $BIN -s $SCRIPTS -d $DEV  || exit -1
fi
if (($TEST_PCQ > 0)); then
    ./smoke/test_pcq.sh $VGARG -b $BIN -s $SCRIPTS -d $DEV  || exit -1
fi
if (($ERRS > 0)); then
    sleep "${SLEEP_TIME}"
    ./smoke/test_errors.sh  -b $BIN -s $SCRIPTS -d $DEV -k $KMOD  || exit -1
else
    echo "skipping test_errors.sh because -n|--noerrors was specified"
fi

sudo umount $MPT

set +x
echo "-------------------------------------------------------------------"
echo "run_smoke completed successfully ($(date))"
echo "-------------------------------------------------------------------"
exit 0
#sleep 4
#./scripts/teardown.sh
