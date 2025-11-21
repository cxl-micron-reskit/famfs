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
TEST="test2"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Start with a clean, empty file systeem
famfs_recreate "test2"

verify_mounted $DEV $MPT "test2.sh"
${CLI} fsck $MPT || fail "fsck should succeed"

# Try to create a file that is not in a famfs file system (assume relative path not in one)
NOT_IN_FAMFS=no_leading_slash
${CLI} creat -s 0x400000 $NOT_IN_FAMFS \
     && fail "creating file not in famfs file system should fail"

# Famfs getmap should succeed on a file that exists
LOG=$MPT/.meta/.log
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    ${CLI} getmap -h   || fail "getmap -h should succeed"
    ${CLI} getmap      && fail "getmap with no file arg should fail"
    ${CLI} getmap badfile  && fail "getmap on nonexistent file should fail"
    ${CLI} getmap -c badfile  && fail "getmap -c on nonexistent file should fail"
    ${CLI} getmap /etc/passwd && fail "getmap on non-famfs file should fail"
    ${CLI} getmap $LOG || fail "getmap should succeed on the famfs log file"
    ${CLI} getmap -q $LOG || fail "getmap -q should succeed on the famfs log file"

    # famfs getmap should fail on a file that does not exist
    NOTEXIST=$MPT/not_exist
    ${CLI} getmap $NOT_EXIST && fail "getmap should fail non nonexistent file in famfs"

    # famfs getmap should fail on a file that is not in a famfs file system
    ${CLI} getmap $NOT_IN_FAMFS && fail "getmap should fail if file not in famfs"
fi

F=bigtest
SIZE=0x4000000
for N in 10 11 12 13 14 15
do
    FILE=${F}${N}
    ${CLI} creat -r -S $N -s $SIZE $MPT/$FILE || fail "creat $FILE"
    ${CLI} verify -S $N -f $MPT/$FILE                || fail "$FILE mismatch"
done

for N in 10 11 12 13 14 15
do
    FILE=${F}${N}
    ${CLI} verify -S $N -f $MPT/$FILE                || fail "$FILE mismatch"
done

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
	 http://localhost/icache_stats
fi

sudo $UMOUNT $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
${MOUNT} $DEV $MPT || fail "mount should succeed"
verify_mounted $DEV $MPT "test1.sh"

sudo cmp $MPT/bigtest10 $MPT/bigtest11        && fail "files should not match"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test2.shadow $MPT

set +x
echo ":==*************************************************************************"
echo ":==test2 completed successfully"
echo ":==*************************************************************************"
exit 0
