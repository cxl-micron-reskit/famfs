#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=scripts
BIN=debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
DURATION=30

if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi

while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
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
	(-t|--time)
	    DURATION=$1
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

CLI="sudo $VG $BIN/famfs"

TEST="pcq_send"

STATUSFILE=./pcqstatus.txt
PCQ="sudo $VG $BIN/pcq  "
pcq="$VG $BIN/pcq  "

ST="--statusfile $STATUSFILE"

source scripts/test_funcs.sh

set -x

DIR="$MPT/pcq0"

# Create some queues

${CLI} logplay $MPT


# Set ownership to the non-privileged caller of this script, so tests run a non-root
# This is important because root can write even without write permissions and we
# want to enforce producer/consumer roles via appropriate file permissions
id=$(id -un)
grp=$(id -gn)
sudo chown -R $id:$grp $DIR

sudo ${PCQ} -setperm p $DIR/q0 || fail "setperm q0"
sudo ${PCQ} -setperm p $DIR/q1 || fail "setperm q1"
sudo ${PCQ} -setperm p $DIR/q2 || fail "setperm q2"
sudo ${PCQ} -setperm p $DIR/q3 || fail "setperm q3"
sudo ${PCQ} -setperm p $DIR/q4 || fail "setperm q4"

# Do a timed run on each queue
echo "$DURATION second run in progress on q0..."
${pcq} -c -s 1 --time $DURATION $DIR/q0 || fail "send$DURATION seconds q0"
echo "$DURATION second run in progress on q1..."
${pcq} -c -s 1 --time $DURATION $DIR/q1 || fail "send $DURATION seconds q1"
echo "$DURATION second run in progress on q2..."
${pcq} -c -s 1 --time $DURATION $DIR/q2 || fail "send $DURATION seconds q2"
echo "$DURATION second run in progress on q3..."
${pcq} -c -s 1 --time $DURATION $DIR/q3 || fail "send $DURATION seconds q3"
echo "$DURATION second run in progress on q4..."
${pcq} -c -s 1 --time $DURATION $DIR/q4 || fail "send $DURATION seconds q4"

${pcq} --drain -v $DIR/q0   || fail "drain empty q0"
${pcq} --drain -v $DIR/q1   || fail "drain empty q1"
${pcq} --drain -v $DIR/q2   || fail "drain empty q2"
${pcq} --drain -v $DIR/q3   || fail "drain empty q3"
${pcq} --drain -v $DIR/q4   || fail "drain empty q4"
