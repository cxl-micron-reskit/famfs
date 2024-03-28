#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/../release"
SCRIPTS="$CWD"
#MOUNT_OPTS="-t famfs -o noatime -o dax=always "
TEST_ALL=1
SLEEP_TIME=2


# Allow these variables to be set from the environment
if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$DEV" ]; then
    DEV="/dev/pmem0"
fi

# Verify that this script is running as root
if [[ $EUID -ne 0 ]]; then
    echo "ERROR: You need to run stress tests as root"
    exit -1
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
        (-f|--fiopath)
	    FIO_PATH=$1
	    shift;
	    ;;
        (-N|--nosleep)
            SLEEP_TIME=0
            ;;
        (-g|--debug)
            BIN="$CWD/../debug"
            echo "debug BIN=$BIN"
            ;;
        (-c|--coverage)
            BIN="$CWD/../coverage"
            echo "coverage BIN=$BIN"
            ;;
        *)
            echo "Unrecognized command line arg: $flag"
            ;;
    esac
done

CLI="sudo $BIN/famfs"

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

./chk_memdev.sh "$DEV" || fail "Bad memory device $DEV"

if [ ! -x "$FIO_PATH/fio" ]; then
    fio -h || fail "Could not find fio, please install fio or provide option -f /path/to/fio/dir"
fi


./stress_prepare.sh -b $BIN -s $SCRIPTS -d $DEV  || exit -1
# For now just run fio, when adding new stress tests, update the infra on how to call them
./stress_fio.sh -b $BIN -s $SCRIPTS -d $DEV -f $FIO_PATH || exit -1


#TODO: Teardown the Fs after the test
