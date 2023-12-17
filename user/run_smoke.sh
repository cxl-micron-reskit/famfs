#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/debug"
SCRIPTS="$CWD/scripts"
DEV="/dev/pmem0"
KMOD="../kmod"
TEST_ERRORS=1

# Check if we have password-less sudi, which is required
sudo -n true 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: password-less sudo capability is required to run smoke tests"
fi
# Make famfs is not mounted or loaded
if (( $(grep -c famfs /proc/mounts) > 0)); then
   echo "Error: famfs is mounted, and must be unmounted to run smoke tests; run scripts/teardown.sh"
   exit -1
fi
if (( $(lsmod | grep -c famfs) > 0)); then
    echo "Error: famfs kernel module must be unloaded to run smoke tests; run scripts/teardown.sh"
    exit -1
fi

while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
	(-n|--noerrors)
	    TEST_ERRORS=0
	    shift;
	    ;;
	*)
	    remainder="$flag $1";
	    shift;
	    while (( $# > 0)); do
		remainder="$remainder $1"
		shift
	    done
	    echo "ignoring commandline remainder: $remainder"
	    ;;
    esac
done

echo "CWD:         $CWD"
echo "BIN:         $BIN"
echo "SCRIPTS:     $SCRIPTS"
echo "TEST_ERRORS: $TEST_ERRORS"

TEST_FUNCS=$SCRIPTS/test_funcs.sh
if [ ! -f $TEST_FUNCS ]; then
    echo "Can't source $TEST_FUNCS"
    exit -1
fi
BIN=debug
if [ ! -d $BIN ]; then
    echo "Can't find executables"
    exit -1
fi
if [! -x "$BIN/famfs" ]; then
    echo "famfs cli missing or not built"
    exit -1
fi

source $TEST_FUNCS

./smoke/test0.sh -b $BIN -s $SCRIPTS -d $DEV -k $KMOD  || exit -1
sleep 4
./smoke/test1.sh -b $BIN -s $SCRIPTS -d $DEV -k $KMOD  || exit -1
sleep 4
./smoke/test2.sh -b $BIN -s $SCRIPTS -d $DEV -k $KMOD  || exit -1
sleep 4
./smoke/test3.sh -b $BIN -s $SCRIPTS -d $DEV -k $KMOD  || exit -1
sleep 4
./smoke/test4.sh -b $BIN -s $SCRIPTS -d $DEV -k $KMOD  || exit -1
if (($TEST_ERRORS > 0)); then
    sleep 4
    ./smoke/test_errors.sh || exit
else
    echo "skipping test_errors.sh because -n|--noerrors was specified"
fi
#sleep 4
#./scripts/teardown.sh
