#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/release"
SCRIPTS="$CWD/scripts"
#MOUNT_OPTS="-t famfs -o noatime -o dax=always "
TEST_ALL=1
SLEEP_TIME=2
RUNTIME=60
FIO_PATH=""
SIZE=0  # Amount of space to consume for files, 100% if no user input
NJOBS=0 # Use all cpus for jobs if 0

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
        (-f|--fiopath)
	    FIO_PATH=$1
	    shift;
	    ;;
        (-N|--nosleep)
            SLEEP_TIME=0
            ;;
        (-g|--debug)
            BIN="$CWD/debug"
            echo "debug BIN=$BIN"
            ;;
        (-r|--runtime)
            RUNTIME=$1
	    shift;
            ;;
        (-s|--size) # Amount of space to be used by this test
            SIZE=$1
	    shift;
            ;;
        (-j|--jobs) # Number of jobs/cpu to be used by this test
            NJOBS=$1
	    shift;
            ;;
        *)
            echo "Unrecognized command line arg: $flag"
	    exit 0
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

if [[ $DEV == *"pmem"* ]]; then
    echo "Pmem devices are not supported"
    exit -1
fi

$SCRIPTS/chk_memdev.sh "$DEV" || fail "Bad memory device $DEV"

# If fio is not present in given path, check for system installed fio
if [ ! -f "$FIO_PATH" ]; then
    echo "Invalid fio path, using fio in system path"
    fio -h > /dev/null || fail "Could not find fio, please install fio or provide option -f /path/to/fio_bin"
    FIO_PATH=$(which fio)
fi

$SCRIPTS/stress_prepare.sh -d $DEV  || exit -1

# For now just run fio, when adding new stress tests, update the infra to call them

$SCRIPTS/stress_fio.sh -d $DEV -f $FIO_PATH -r $RUNTIME -s $SIZE -j $NJOBS || echo "Fio stress test failed"

echo ""
echo "*************************************************************************************"
echo "                    run_stress_tests.sh completed"
echo "*************************************************************************************"

