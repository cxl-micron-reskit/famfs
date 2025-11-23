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
	FSCK_OPTS="--nodax"
    else
        MOUNT_OPTS="--nofuse" # Can drop this b/c fuse is the default
	MKFS_OPTS=""
	FSCK_OPTS=""
    fi
else
    echo "FAMFS_MODE: invalid"
    exit 1;
fi

MOUNT="sudo $VG $BIN/famfs mount $MOUNT_OPTS"
MKFS="sudo $VG $BIN/mkfs.famfs $MKFS_OPTS"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
FSCK="${CLI} fsck $FSCK_OPTS"
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
expect_good ${PCQ} -? -- "pcq help should work"
expect_fail ${PCQ} --info --create           -- "pcq should fail with --info and --create"
expect_fail ${PCQ} --drain --producer        -- "pcq should fail with --drain and --producer"
expect_fail ${PCQ} --time 10 --nmessages 100 -- "pcq should fail with --time and --nmessages"
expect_fail ${PCQ} --create                  -- "pcq --creaete should fail with no bzsize"
expect_fail ${PCQ} --create -D -v --bsize 1024 --nbuckets 1024 -- "should fail with missing filename"
expect_fail ${PCQ} --create -D -v --bsize 1024 --nbuckets 1024 -- "Create should fail with no file"

# Create some queues
expect_good ${PCQ} --create $uarg -D -v --bsize 1024 --nbuckets 1024 $MPT/q0 -- "basic pcq create 0"
expect_good ${PCQ} --create $uarg -v --bsize 64   --nbuckets 1K   $MPT/q1 -- "basic pcq create 1"
expect_good ${PCQ} --create $uarg -v --bsize 64K  --nbuckets 512  $MPT/q2 -- "basic pcq create 2"
expect_good ${PCQ} --create $uarg -v --bsize 512K --nbuckets 1k   $MPT/q3 -- "basic pcq create 3"
expect_good ${PCQ} --create $uarg -v --bsize 256K --nbuckets 256  $MPT/q4 -- "basic pcq create 4"

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
    expect_good ${pcq} --setperm c $MPT/q0        -- "setperm c should work"
    expect_fail test -w $MPT/q0                   -- "setperm c should remove write permission on q0"
    expect_good test -w $MPT/q0.consumer          -- "setperm c should restore write permission on q0.consumer"
    expect_good ${pcq} --info -v $MPT/q0          -- "pcq info perm c should work"
    expect_fail ${pcq} --producer -v -N 1 $MPT/q0 -- "producer should fail with c permission on q0"

    # Producer permission
    expect_good ${pcq} --setperm p $MPT/q0        -- "setperm p should work"
    expect_good test -w $MPT/q0                   -- "setperm p should restore write permission on q0"
    expect_fail test -w $MPT/q0.consumer          -- "setperm p should remove write permission on q0.consumer"
    expect_good ${pcq} --info -v $MPT/q0          -- "pcq info perm p should work"
    expect_fail ${pcq} --consumer -N 1 -v $MPT/q0 -- "consumer should fail with p permission on q0"
    expect_fail ${pcq} --drain -v $MPT/q0         -- "drain should fail with p permission on q0"

    # Neither permission (read-only on both files)
    expect_good ${pcq} --setperm n $MPT/q0        -- "setperm n should work"
    expect_fail test -w $MPT/q0                   -- "setperm n should remove write permission on q0"
    expect_fail test -w $MPT/q0.consumer          -- "setperm n should remove write permission on q0.consumer"
    expect_good ${pcq} --info -v $MPT/q0          -- "pcq info perm n should work"
    expect_fail ${pcq} --producer -v -N 1 $MPT/q0 -- "producer should fail with n permission on q0"
    expect_fail ${pcq} --consumer -N 1 -v $MPT/q0 -- "consumer should fail with n permission on q0"

    # Producer and consumer permission (both files writable)
    expect_good ${pcq} --setperm b $MPT/q0  -- "setperm b should work"
    expect_good test -w $MPT/q0             -- "setperm b should restore write permission on q0"
    expect_good test -w $MPT/q0.consumer    -- "setperm b should restore write permission on q0.consumer"
    expect_good ${pcq} --info -v $MPT/q0    -- "pcq info perm n should work"
fi

expect_good ${pcq} --info -v $MPT/q0    -- "basic pcq info 0"
expect_good ${pcq} --info -v $MPT/q1    -- "basic pcq info 1"
expect_good ${pcq} --info -v $MPT/q2    -- "basic pcq info 2"
expect_good ${pcq} --info -v $MPT/q3    -- "basic pcq info 3"
expect_good ${pcq} --info -v $MPT/q4    -- "basic pcq info 4"

expect_good ${pcq} --drain -v $MPT/q0   -- "drain empty q0"
expect_good ${pcq} --drain -v $MPT/q1   -- "drain empty q1"
expect_good ${pcq} --drain -v $MPT/q2   -- "drain empty q2"
expect_good ${pcq} --drain -v $MPT/q3   -- "drain empty q3"
expect_good ${pcq} --drain -v $MPT/q4   -- "drain empty q4"

# put 128 entries in each queue
expect_good ${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q0 -- "put 128 in q0"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q0"
expect_good ${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q1 -- "put 128 in q1"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q1"
expect_good ${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q2 -- "put 128 in q2"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q2"
expect_good ${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q3 -- "put 128 in q3"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q3"
expect_good ${pcq} --producer -v -N 128 --statusfile $STATUSFILE $MPT/q4 -- "put 128 in q4"
assert_equal $(cat $STATUSFILE) 128 "put 128 in q4"

# consume half of the elements from each queue
expect_good ${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q0 -- "consume 64 from q0"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
expect_good ${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q1 -- "consume 64 from q1"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
expect_good ${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q2 -- "consume 64 from q2"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
expect_good ${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q3 -- "consume 64 from q3"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"
expect_good ${pcq} --consumer -N 64 -v --statusfile $STATUSFILE $MPT/q4 -- "consume 64 from q4"
assert_equal $(cat $STATUSFILE) 64 "consume 64 from q0"

# Drain the rest of the elements
expect_good ${pcq} --drain -v --statusfile $STATUSFILE $MPT/q0 -- "drain 64 from q0"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q0"
expect_good ${pcq} --drain -v --statusfile $STATUSFILE $MPT/q1 -- "drain 64 from q1"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q1"
expect_good ${pcq} --drain -v --statusfile $STATUSFILE $MPT/q2 -- "drain 64 from q2"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q2"
expect_good ${pcq} --drain -v --statusfile $STATUSFILE $MPT/q3 -- "drain 64 from q3"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q3"
expect_good ${pcq} --drain -v --statusfile $STATUSFILE $MPT/q4 -- "drain 64 from q4"
assert_equal $(cat $STATUSFILE) 64 "drain 64 from q4"

# Run simultaneous producer/consumer for 1K messages with seed verificatino
expect_good ${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q0 -- "p/c 1m in q0"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q0"
expect_good ${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q1 -- "p/c 1m in q1"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q1"
expect_good ${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q2 -- "p/c 1m in q2"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q2"
expect_good ${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q3 -- "p/c 1m in q3"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q3"
expect_good ${pcq} -pc --seed 43 -N 1000 --statusfile $STATUSFILE $MPT/q4 -- "p/c 1m in q4"
assert_equal $(cat $STATUSFILE) 2000 "produce/consume 1m with q4"

# Run simultaneous producer/consumer for 10K messages on each queue
expect_good ${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q0 -- "p/c 1m in q0"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q0"
expect_good ${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q1 -- "p/c 1m in q1"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q1"
expect_good ${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q2 -- "p/c 1m in q2"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q2"
expect_good ${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q3 -- "p/c 1m in q3"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q3"
expect_good ${pcq} -pc -s 1 -N 10000 --statusfile $STATUSFILE $MPT/q4 -- "p/c 1m in q4"
assert_equal $(cat $STATUSFILE) 20000 "produce/consume 1m with q4"

# Do a timed run on each queue
echo "10 second run in progress on q0..."
expect_good ${pcq} -pc --time 10 $MPT/q0                -- "p/c 10 seconds q0"
echo "10 second run in progress on q1..."
expect_good ${pcq} -pc -s 1 --time 10 $MPT/q1           -- "p/c 10 seconds q1"
echo "10 second run in progress on q2..."
expect_good ${pcq} -pc -s 1 --time 10 $MPT/q2           -- "p/c 10 seconds q2"
echo "10 second run in progress on q3..."
expect_good ${pcq} -pc -s 1 --time 10 $MPT/q3           -- "p/c 10 seconds q3"
echo "10 second run in progress on q4..."
expect_good ${pcq} -pc -s 1 --time 10 $MPT/q4           -- "p/c 10 seconds q4"

expect_good ${pcq} --info -v $MPT/q0                        -- "maybe not empty pcq info 0"
expect_good ${pcq} --info -v $MPT/q1                        -- "maybe not empty pcq info 1"
expect_good ${pcq} --info -v $MPT/q2                        -- "maybe not empty pcq info 2"
expect_good ${pcq} --info -v $MPT/q3                        -- "maybe not empty pcq info 3"
expect_good ${pcq} --info -v $MPT/q4                        -- "maybe not empty pcq info 4"

# Drain the queues beause there are probably some un-consumed messages in there
expect_good ${pcq} --drain $MPT/q0 -- "drain q0"
expect_good ${pcq} --drain $MPT/q1 -- "drain q1"
expect_good ${pcq} --drain $MPT/q2 -- "drain q2"
expect_good ${pcq} --drain $MPT/q3 -- "drain q3"
expect_good ${pcq} --drain $MPT/q4 -- "drain q4"

expect_good ${pcq} --info $MPT/q0                        -- "empty pcq info 0"
expect_good ${pcq} --info $MPT/q1                        -- "empty pcq info 1"
expect_good ${pcq} --info $MPT/q2                        -- "empty pcq info 2"
expect_good ${pcq} --info $MPT/q3                        -- "empty pcq info 3"
expect_good ${pcq} --info $MPT/q4                        -- "empty pcq info 4"

unlink "$STATUSFILE"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_pcq.shadow $MPT

set +x
echo ":========================================================================"
echo ":== test_pcq.sh: success!"
echo ":========================================================================"

exit 0
