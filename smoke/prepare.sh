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
	    echo "FAMFS_MOD=${FAMFS_MOD}"
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
        MOUNT_OPTS="-f"
    fi
else
    echo "FAMFS_MODE: invalid"
    exit 1;
fi

MOUNT="sudo $VG $BIN/famfs mount $MOUNT_OPTS"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
TEST="prepare"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

sudo mkdir -p $MPT || fail "mkdir $MPT"

# Make sure famfs is not mounted
findmnt $MPT
if (( $? == 0 )); then
    sudo umount $MPT
fi
sudo umount /tmp/famfs_fuse

# destroy famfs file system, if any
${MKFS} -h            || fail "mkfs -h should work"
${MKFS}               && fail "mkfs without dev argument should fail"
${MKFS} /tmp/nonexistent && fail "mkfs on nonexistent dev should fail"
# in case there is not a file system, make one
${MKFS} $dev # no error check; if there is not a fs already this will create it
${MKFS} -k $DEV       && fail "mkfs/kill should fail without --force"
${MKFS} -f -k $DEV    || fail "mkfs/kill should succeed wtih --force"
${MKFS}  $DEV         || fail "mkfs"
${MKFS} -f $DEV       || fail "redo mkfs with -f should succeed"

${MKFS} -f --loglen 1 $DEV && fail "mkfs with loglen 1 should fail"
LOG_LEN=$(sudo $BIN/famfs fsck -v $MPT | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen"

${MKFS} -f --loglen 11m $DEV && fail "mkfs with loglen 11m should fail"
LOG_LEN=$(sudo $BIN/famfs fsck -v $MPT | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen 2"

${MKFS} -f --loglen 256m $DEV || fail "mkfs should work with 256m log"
LOG_LEN=$(sudo $BIN/famfs fsck -v $MPT | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log should be 256M"

${MKFS} $DEV && fail "redo mkfs without -f should fail"
LOG_LEN=$(sudo $BIN/famfs fsck -v $MPT | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log size should not change after failed mkfs"

${MKFS} -f --loglen 1m $DEV   && fail "mkfs should fail with 1m logsize"
LOG_LEN=$(sudo $BIN/famfs fsck -v $MPT | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log size should not change after mkfs with bogus loglen 3"

${MKFS} -f $DEV       || fail "redo mkfs with -f should succeed 2"
LOG_LEN=$(sudo $BIN/famfs fsck -v $MPT | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen4"

${MKFS}  $DEV         && fail "mkfs redo" # fail, fs exists

${CLI} -h || fail "cli -h should succeed"
${CLI} fsck $DEV          || fail "fsck"

if [[ "$FAMFS_MODE" == "v1" ]]; then
    # We now expect the module to already be loaded (if FAMFS_MODE==v1),
    # but no harm in modprobe to make double sure
    sudo modprobe ${FAMFS_MOD} || fail "modprobe ${FAMFS_MOD}"

    #
    # Test manual mount / mkmeta / logplay
    #
    sudo mount $RAW_MOUNT_OPTS $DEV $MPT || fail "mount"
    sudo mount $RAW_MOUNT_OPTS $DEV $MPT && fail "double mount should fail"

    ${CLI} mkmeta $DEV        || fail "mkmeta"

    # XXX famfs-fuse does not yet put the primary daxdev in /proc/mounts.
    # need to fix this
    grep $DEV /proc/mounts              || fail "dev=$DEV not in /proc/mounts~"
else
    # In fuse mode, we don't support manual mount...
    ${MOUNT} $DEV $MPT || fail "famfs fuse mount should work"
fi

grep famfs /proc/mounts             || fail "No famfs mounted"
grep $MPT /proc/mounts              || fail "Mount pt $MPT not in /proc/mounts~"

sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "prep: no log file after mkmeta"

${CLI} logplay $MPT || fail "empty fs logplay should succeed"
${CLI} fsck --human $MPT || fail "prep: fsck --human should succeed"

# Try mkfs while mounted
${MKFS}  $DEV         && fail "mkfs while mounted should fail"
${MKFS} -f -k $DEV    && fail "mkfs/kill while mounted should fail"

#
# Blow away the file system and test famfs mount with no valid superblock
#
sudo umount $MPT       || fail "umount $MPT should succeed"
verify_not_mounted $DEV $MPT "umount failed?"
${MKFS} -f -k $DEV     || fail "mkfs/kill should succeed wtih --force"
${MOUNT} $DEV $MPT     && fail "famfs mount should fail with invalid superblock"
${MKFS} $DEV           || fail "clean mkfs should succeed"
${MOUNT} $DEV $MPT     || fail "mount of clean file system should succeed"
${MOUNT} $DEV $MPT     && fail "Double mount should fail "

verify_mounted $DEV $MPT "mount failed?"

set +x
echo "*************************************************************************"
echo "prepare completed successfully"
echo "*************************************************************************"
exit 0
