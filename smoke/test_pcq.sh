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
    else
        MOUNT_OPTS="--nofuse" # Can drop this b/c fuse is the default
	MKFS_OPTS=""
    fi
else
    echo "FAMFS_MODE: invalid"
    exit 1;
fi

MOUNT="sudo $VG $BIN/famfs mount $MOUNT_OPTS"
MKFS="sudo $VG $BIN/mkfs.famfs $MKFS_OPTS"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
TEST="pcq"

STATUSFILE="/tmp/pcqstatus_$$.txt"
PCQ="sudo $VG $BIN/pcq  "
pcq="$VG $BIN/pcq  "

source scripts/test_funcs.sh

set -x

# Start with a clean, empty file systeem
famfs_recreate "test_pcq"

id=$(id -un)
grp=$(id -gn)
uarg="-u $id -g $grp"

# Test some bogus command line combinations
${PCQ} -? || fail "pcq help should work"
${PCQ} --info --create           && fail "pcq should fail with --info and --create"
${PCQ} --drain --producer        && fail "pcq should fail with --drain and --producer"
${PCQ} --time 10 --nmessages 100 && fail "pcq should fail with --time and --nmessages"
${PCQ} --create                  && fail "pcq --creaete should fail with no bzsize"
${PCQ} --create -D -v --bsize 1024 --nbuckets 1024 && fail "should fail with missing filename"
${PCQ} --create -D -v --bsize 1024 --nbuckets 1024 && fail "Create should fail with no file"

# Create some queues
${PCQ} --create $uarg -D -v --bsize 1024 --nbuckets 1024 $MPT/q0 || fail "basic pcq create 0"
${PCQ} --create $uarg -v --bsize 64   --nbuckets 1K   $MPT/q1 || fail "basic pcq create 1"
${PCQ} --create $uarg -v --bsize 64K  --nbuckets 512  $MPT/q2 || fail "basic pcq create 2"
${PCQ} --create $uarg -v --bsize 512K --nbuckets 1k   $MPT/q3 || fail "basic pcq create 3"
${PCQ} --create $uarg -v --bsize 256K --nbuckets 256  $MPT/q4 || fail "basic pcq create 4"

# Set ownership to the non-privileged caller of this script, so tests run a non-root
# This is important because root can write even without write permissions and we
# want to enforce producer/consumer roles via appropriate file permissions
sudo chown $id:$grp $MPT/q0
sudo chown $id:$grp $MPT/q1
sudo chown $id:$grp $MPT/q2
sudo chown $id:$grp $MPT/q3
sudo chown $id:$grp $MPT/q4
sudo chown $id:$grp $MPT/q0.consumer
sudo chown $id:$grp $MPT/q1.consumer
sudo chown $id:$grp $MPT/q2.consumer
sudo chown $id:$grp $MPT/q3.consumer
sudo chown $id:$grp $MPT/q4.consumer

# From here on we run the non-sudo ${pcq} rather than the sudo ${PCQ}

# Coverage tests leave some root cruft after running pcq as root; clean that up...
sudo chown -R ${id}:${grp} $BIN

# Test setperm
# XXX: --setperm not working yet on fuse as of 9/2025
# because 'chmod' is not yet implemented; fix this...
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    # XXX review these tests to see if they make sense;
    # setperm was broken until this commit
    # Consumer permission
    ${pcq} --setperm c $MPT/q0        || fail "setperm c should work"
    test -w $MPT/q0                   && fail "setperm c should remove write permission on q0"
    test -w $MPT/q0.consumer          || fail "setperm c should restore write permission on q0.consumer"
    ${pcq} --info -v $MPT/q0          || fail "pcq info perm c should work"
    ${pcq} --producer -v -N 1 $MPT/q0 && fail "producer should fail with c permission on q0"

    # Producer permission
    ${pcq} --setperm p $MPT/q0        || fail "setperm p should work"
    test -w $MPT/q0                   || fail "setperm p should restore write permission on q0"
    test -w $MPT/q0.consumer          && fail "setperm p should remove write permission on q0.consumer"
    ${pcq} --info -v $MPT/q0          || fail "pcq info perm p should work"
    ${pcq} --consumer -N 1 -v $MPT/q0 && fail "consumer should fail with p permission on q0"
    ${pcq} --drain -v $MPT/q0         && fail "drain should fail with p permission on q0"

    # Neither permission (read-only on both files)
    ${pcq} --setperm n $MPT/q0        || fail "setperm n should work"
    test -w $MPT/q0                   && fail "setperm n should remove write permission on q0"
    test -w $MPT/q0.consumer          && fail "setperm n should remove write permission on q0.consumer"
    ${pcq} --info -v $MPT/q0          || fail "pcq info perm n should work"
    ${pcq} --producer -v -N 1 $MPT/q0 && fail "producer should fail with n permission on q0"
    ${pcq} --consumer -N 1 -v $MPT/q0 && fail "consumer should fail with n permission on q0"

    # Producer and consumer permission (both files writable)
    ${pcq} --setperm b $MPT/q0  || fail "setperm b should work"
    test -w $MPT/q0             || fail "setperm b should restore write permission on q0"
    test -w $MPT/q0.consumer    || fail "setperm b should restore write permission on q0.consumer"
    ${pcq} --info -v $MPT/q0    || fail "pcq info perm n should work"
fi

${pcq} --info -v $MPT/q0    || fail "basic pcq info 0"
${pcq} --info -v $MPT/q1    || fail "basic pcq info 1"
${pcq} --info -v $MPT/q2    || fail "basic pcq info 2"
${pcq} --info -v $MPT/q3    || fail "basic pcq info 3"
${pcq} --info -v $MPT/q4    || fail "basic pcq info 4"

${pcq} --drain -v $MPT/q0   || fail "drain empty q0"
${pcq} --drain -v $MPT/q1   || fail "drain empty q1"
${pcq} --drain -v $MPT/q2   || fail "drain empty q2"
${pcq} --drain -v $MPT/q3   || fail "drain empty q3"
${pcq} --drain -v $MPT/q4   || fail "drain empty q4"

# put 128 entries in each queue
${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q0 || fail "put 128 in q0"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q0"
${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q1 || fail "put 128 in q1"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q1"
${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q2 || fail "put 128 in q2"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q2"
${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q3 || fail "put 128 in q3"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q3"
${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q4 || fail "put 128 in q4"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q4"

# consume half of the elements from each queue
${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q0 || fail "consume 64 from q0"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q1 || fail "consume 64 from q1"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q2 || fail "consume 64 from q2"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q3 || fail "consume 64 from q3"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q4 || fail "consume 64 from q4"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"

# Drain the rest of the elements
${pcq} --drain -v --statusfile $STATUSFILE $MPT/q0 || fail "drain 64 from q0"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q0"
${pcq} --drain -v --statusfile $STATUSFILE $MPT/q1 || fail "drain 64 from q1"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q1"
${pcq} --drain -v --statusfile $STATUSFILE $MPT/q2 || fail "drain 64 from q2"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q2"
${pcq} --drain -v --statusfile $STATUSFILE $MPT/q3 || fail "drain 64 from q3"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q3"
${pcq} --drain -v --statusfile $STATUSFILE $MPT/q4 || fail "drain 64 from q4"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q4"

# Run simultaneous producer/consumer for 1K messages with seed verificatino
${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q0 || fail "p/c 1m in q0"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q0"
${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q1 || fail "p/c 1m in q1"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q1"
${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q2 || fail "p/c 1m in q2"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q2"
${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q3 || fail "p/c 1m in q3"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q3"
${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q4 || fail "p/c 1m in q4"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q4"

# Run simultaneous producer/consumer for 10K messages on each queue
${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q0 || fail "p/c 1m in q0"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q0"
${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q1 || fail "p/c 1m in q1"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q1"
${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q2 || fail "p/c 1m in q2"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q2"
${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q3 || fail "p/c 1m in q3"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q3"
${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q4 || fail "p/c 1m in q4"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q4"

# Do a timed run on each queue
echo "10 second run in progress on q0..."
${pcq} -pc --time 10 $MPT/q0                || fail "p/c 10 seconds q0"
echo "10 second run in progress on q1..."
${pcq} -pc -s 1 --time 10 $MPT/q1           || fail "p/c 10 seconds q1"
echo "10 second run in progress on q2..."
${pcq} -pc -s 1 --time 10 $MPT/q2           || fail "p/c 10 seconds q2"
echo "10 second run in progress on q3..."
${pcq} -pc -s 1 --time 10 $MPT/q3           || fail "p/c 10 seconds q3"
echo "10 second run in progress on q4..."
${pcq} -pc -s 1 --time 10 $MPT/q4           || fail "p/c 10 seconds q4"

${pcq} --info -v $MPT/q0                        || fail "maybe not empty pcq info 0"
${pcq} --info -v $MPT/q1                        || fail "maybe not empty pcq info 1"
${pcq} --info -v $MPT/q2                        || fail "maybe not empty pcq info 2"
${pcq} --info -v $MPT/q3                        || fail "maybe not empty pcq info 3"
${pcq} --info -v $MPT/q4                        || fail "maybe not empty pcq info 4"

# Drain the queues beause there are probably some un-consumed messages in there
${pcq} --drain $MPT/q0 || fail "drain q0"
${pcq} --drain $MPT/q1 || fail "drain q1"
${pcq} --drain $MPT/q2 || fail "drain q2"
${pcq} --drain $MPT/q3 || fail "drain q3"
${pcq} --drain $MPT/q4 || fail "drain q4"

${pcq} --info $MPT/q0                        || fail "empty pcq info 0"
${pcq} --info $MPT/q1                        || fail "empty pcq info 1"
${pcq} --info $MPT/q2                        || fail "empty pcq info 2"
${pcq} --info $MPT/q3                        || fail "empty pcq info 3"
${pcq} --info $MPT/q4                        || fail "empty pcq info 4"

unlink "$STATUSFILE"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_pcq.shadow $MPT

set +x
echo ":========================================================================"
echo ":== test_pcq.sh: success!"
echo ":========================================================================"

exit 0
