#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/debug"
SCRIPTS="$CWD/scripts"
DEV="/dev/pmem0"
KMOD="../kmod"

echo "CWD:     $CWD"
echo "BIN:     $BIN"
echo "SCRIPTS: $SCRIPTS"

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
#sleep 4
#./test_errors.sh || exit
#sleep 4
#./teardown.sh
