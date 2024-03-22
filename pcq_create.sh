#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=scripts
BIN=debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
MPT=/mnt/famfs

if [ -z "$DEV" ]; then
    DEV=/dev/dax0.0
fi
DURATION=30

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

MKFS="sudo mkfs.famfs $DEV $MPT"

sudo umount $MPT
sudo $MKFS  -f $DEV
${CLI} mount  $DEV $MPT
set -x

DIR="$MPT/pcq0"

# Create some queues

${CLI} mkdir $DIR || fail "failed to mkdir $DIR"
${PCQ} --create -D -v --bsize 1024 --nbuckets 1024 $DIR/q0 || fail "basic pcq create 0"
${PCQ} --create -v --bsize 64   --nbuckets 1K   $DIR/q1 || fail "basic pcq create 1"
${PCQ} --create -v --bsize 64K  --nbuckets 512  $DIR/q2 || fail "basic pcq create 2"
${PCQ} --create -v --bsize 512K --nbuckets 1k   $DIR/q3 || fail "basic pcq create 3"
${PCQ} --create -v --bsize 256K --nbuckets 256  $DIR/q4 || fail "basic pcq create 4"

# Set ownership to the non-privileged caller of this script, so tests run a non-root
# This is important because root can write even without write permissions and we
# want to enforce producer/consumer roles via appropriate file permissions
id=$(id -un)
grp=$(id -gn)
sudo chown $id:$grp $DIR
sudo chown -R $id:$grp $DIR


