#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=../scripts
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"

# These can be overridden via the environment
if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$UMOUNT" ]; then
    UMOUNT="umount"
fi
if [ -z "$DEV" ]; then
    DEV="/dev/pmem0"
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
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
TEST="pcq"

STATUSFILE=./pcqstatus.txt
PCQ="sudo $VG $BIN/pcq  "

ST="--statusfile $STATUSFILE"

source scripts/test_funcs.sh

#sudo umount $MPT
#sudo $MKFS  -f $DEV
#${CLI} mount  $DEV $MPT
set -x

${PCQ} -? || fail "pcq help should work"
${PCQ} --info --create           && fail "pcq should fail with --info and --create"
${PCQ} --drain --producer        && fail "pcq should fail with --drain and --producer"
${PCQ} --time 10 --nmessages 100 && fail "pcq should fail with --time and --nmessages"
${PCQ} --create                  && fail "pcq --creaete should fail with no bzsize"
${PCQ} --create -D -v --bsize 1024 --nbuckets 1024 && fail "should fail with missing filename"

# Create some queues
${PCQ} --create -D -v --bsize 1024 --nbuckets 1024 $MPT/q0 || fail "basic pcq create 0"
${PCQ} --create -v --bsize 64   --nbuckets 1K   $MPT/q1 || fail "basic pcq create 1"
${PCQ} --create -v --bsize 64K  --nbuckets 512  $MPT/q2 || fail "basic pcq create 2"
${PCQ} --create -v --bsize 512K --nbuckets 1k   $MPT/q3 || fail "basic pcq create 3"
${PCQ} --create -v --bsize 256K --nbuckets 256  $MPT/q4 || fail "basic pcq create 4"

${PCQ} --info -v $MPT/q0                                || fail "basic pcq info 0"
${PCQ} --info -v $MPT/q1                                || fail "basic pcq info 1"
${PCQ} --info -v $MPT/q2                                || fail "basic pcq info 2"
${PCQ} --info -v $MPT/q3                                || fail "basic pcq info 3"
${PCQ} --info -v $MPT/q4                                || fail "basic pcq info 4"

${PCQ} --drain -v $MPT/q0 || fail "drain empty q0"
${PCQ} --drain -v $MPT/q1 || fail "drain empty q1"
${PCQ} --drain -v $MPT/q2 || fail "drain empty q2"
${PCQ} --drain -v $MPT/q3 || fail "drain empty q3"
${PCQ} --drain -v $MPT/q4 || fail "drain empty q4"

# put 128 entries in each queue
${PCQ} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q0 || fail "put 128 in q0"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q0"
${PCQ} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q1 || fail "put 128 in q1"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q1"
${PCQ} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q2 || fail "put 128 in q2"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q2"
${PCQ} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q3 || fail "put 128 in q3"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q3"
${PCQ} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q4 || fail "put 128 in q4"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q4"

# consume half of the elements from each queue
${PCQ} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q0 || fail "consume 64 from q0"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${PCQ} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q1 || fail "consume 64 from q1"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${PCQ} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q2 || fail "consume 64 from q2"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${PCQ} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q3 || fail "consume 64 from q3"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${PCQ} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q4 || fail "consume 64 from q4"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"

# Drain the rest of the elements
${PCQ} --drain -v --statusfile $STATUSFILE $MPT/q0 || fail "drain 64 from q0"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q0"
${PCQ} --drain -v --statusfile $STATUSFILE $MPT/q1 || fail "drain 64 from q1"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q1"
${PCQ} --drain -v --statusfile $STATUSFILE $MPT/q2 || fail "drain 64 from q2"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q2"
${PCQ} --drain -v --statusfile $STATUSFILE $MPT/q3 || fail "drain 64 from q3"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q3"
${PCQ} --drain -v --statusfile $STATUSFILE $MPT/q4 || fail "drain 64 from q4"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q4"

# Run simultaneous producer/consumer for 1K messages with seed verificatino
${PCQ} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q0 || fail "p/c 1m in q0"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q0"
${PCQ} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q1 || fail "p/c 1m in q1"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q1"
${PCQ} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q2 || fail "p/c 1m in q2"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q2"
${PCQ} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q3 || fail "p/c 1m in q3"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q3"
${PCQ} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q4 || fail "p/c 1m in q4"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q4"

# Run simultaneous producer/consumer for 10K messages on each queue
${PCQ} -pc -N 10000 --statusfile $STATUSFILE $MPT/q0 || fail "p/c 1m in q0"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q0"
${PCQ} -pc -N 10000 --statusfile $STATUSFILE $MPT/q1 || fail "p/c 1m in q1"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q1"
${PCQ} -pc -N 10000 --statusfile $STATUSFILE $MPT/q2 || fail "p/c 1m in q2"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q2"
${PCQ} -pc -N 10000 --statusfile $STATUSFILE $MPT/q3 || fail "p/c 1m in q3"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q3"
${PCQ} -pc -N 10000 --statusfile $STATUSFILE $MPT/q4 || fail "p/c 1m in q4"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q4"

# Do a timed run on each queue
echo "10 second run in progress on q0..."
${PCQ} -pc -v --time 10 --statusfile $STATUSFILE $MPT/q0 || fail "p/c 10 seconds q0"
echo "10 second run in progress on q1..."
${PCQ} -pc --time 10 --statusfile $STATUSFILE $MPT/q1 || fail "p/c 10 seconds q1"
echo "10 second run in progress on q2..."
${PCQ} -pc --time 10 --statusfile $STATUSFILE $MPT/q2 || fail "p/c 10 seconds q2"
echo "10 second run in progress on q3..."
${PCQ} -pc --time 10 --statusfile $STATUSFILE $MPT/q3 || fail "p/c 10 seconds q3"
echo "10 second run in progress on q4..."
${PCQ} -pc --time 10 --statusfile $STATUSFILE $MPT/q4 || fail "p/c 10 seconds q4"

${PCQ} --info -v $MPT/q0                        || fail "maybe not empty pcq info 0"
${PCQ} --info -v $MPT/q1                        || fail "maybe not empty pcq info 1"
${PCQ} --info -v $MPT/q2                        || fail "maybe not empty pcq info 2"
${PCQ} --info -v $MPT/q3                        || fail "maybe not empty pcq info 3"
${PCQ} --info -v $MPT/q4                        || fail "maybe not empty pcq info 4"

# Drain the queues beause there are probably some un-consumed messages in there
${PCQ} --drain $MPT/q0 || fail "drain q0"
${PCQ} --drain $MPT/q1 || fail "drain q1"
${PCQ} --drain $MPT/q2 || fail "drain q2"
${PCQ} --drain $MPT/q3 || fail "drain q3"
${PCQ} --drain $MPT/q4 || fail "drain q4"

${PCQ} --info -v $MPT/q0                        || fail "empty pcq info 0"
${PCQ} --info -v $MPT/q1                        || fail "empty pcq info 1"
${PCQ} --info -v $MPT/q2                        || fail "empty pcq info 2"
${PCQ} --info -v $MPT/q3                        || fail "empty pcq info 3"
${PCQ} --info -v $MPT/q4                        || fail "empty pcq info 4"

set +x
echo "======================================================================"
echo " test_pcq.sh: success!"
echo "======================================================================"

exit 0
