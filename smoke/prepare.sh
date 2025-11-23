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
        MOUNT_OPTS="--fuse" # Can drop this b/c fuse is the default
	MKFS_OPTS="--nodax"
	FSCK_OPTS="--nodax"
    else
        MOUNT_OPTS="--nofuse" # Can drop this b/c fuse isf the default
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
TEST="prepare"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

#set -x

sudo mkdir -p $MPT || fail "mkdir $MPT"

# Make sure famfs is not mounted
findmnt $MPT
if (( $? == 0 )); then
    sudo umount $MPT
fi
sudo umount /tmp/famfs_fuse

# destroy famfs file system, if any
expect_good ${MKFS} -h    -- "mkfs -h should work"
expect_fail ${MKFS}       -- "mkfs without dev argument should fail"

expect_fail ${MKFS} /tmp/nonexistent -- "mkfs on nonexistent dev should fail"

# in case there is not a file system, make one. Failure OK, but not crashes
stop_on_crash ${MKFS} $DEV -- "Safety mkfs"

expect_fail ${MKFS} -k $DEV -- "mkfs/kill should fail without --force"
expect_good ${MKFS} -f -k $DEV -- "mkfs/kill should succeed with --force"
expect_good ${MKFS}  $DEV      -- "mkfs"
expect_good ${MKFS} -f $DEV    -- "redo mkfs with -f should succeed"

expect_fail ${MKFS} -f --loglen 1 $DEV -- "mkfs with loglen 1 should fail"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen"

${MKFS} -f --loglen 11m $DEV && fail "mkfs with loglen 11m should fail"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen 2"

#exit 1
${MKFS} -f --loglen 256m $DEV || fail "mkfs should work with 256m log"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log should be 256M"

${MKFS} $DEV && fail "redo mkfs without -f should fail"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log size should not change after failed mkfs"

${MKFS} -f --loglen 1m $DEV   && fail "mkfs should fail with 1m logsize"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "268435456" "Log size should not change after mkfs with bogus loglen 3"

${MKFS} -f $DEV       || fail "redo mkfs with -f should succeed 2"
LOG_LEN=$(expect_good ${FSCK} -v $DEV | grep "log_len" | awk -e '{print $2}')
assert_equal $LOG_LEN "8388608" "Log size should not change after mkfs with bogus loglen4"

expect_fail ${MKFS}  $DEV   -- "mkfs redo" # fail, fs exists

expect_good ${CLI} -h        -- "cli -h should succeed"
expect_good ${FSCK} $DEV -- "fsck"

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
    ${MOUNT} -vv $DEV $MPT || fail "famfs fuse mount should work"
fi

expect_good grep famfs /proc/mounts -- "No famfs mounted"
expect_good grep $MPT /proc/mounts  -- "Mount pt $MPT not in /proc/mounts~"

expect_good sudo test -f $MPT/.meta/.superblock -- "no superblock file after mkmeta"
expect_good sudo test -f $MPT/.meta/.log -- "prep: no log file after mkmeta"

expect_good ${CLI} logplay $MPT  -- "empty fs logplay should succeed"
expect_good ${FSCK} --human $MPT -- "prep: fsck --human should succeed"

# Try mkfs while mounted
expect_fail ${MKFS}  $DEV        -- "mkfs while mounted should fail"

# XXX: currently after fuse mount, daxdev is not open unless a file has
# been looked up and accessed.
#exit -1

#${MKFS} -f -k $DEV    && fail "mkfs/kill while mounted should fail"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    expect_good sudo curl  --unix-socket \
		$(scripts/famfs_shadow.sh /mnt/famfs)/sock \
		http://localhost/icache_stats -- "REST query failed"
fi

#
# Blow away the file system and test famfs mount with no valid superblock
#
expect_good sudo umount $MPT       -- "umount $MPT should succeed"
verify_not_mounted $DEV $MPT "umount failed?"

# We don't know for certain whether there is a valid file system, so create one...
expect_good ${MKFS} -f $DEV        -- "mkfs should succeed with --force"
expect_good ${MKFS} -f -k $DEV     -- "mkfs/kill should succeed with --force (2)"
# Now there is not a valid file system because we killed the superblock...
expect_fail_except --bad 99 \
		   ${MOUNT} $DEV $MPT -- "mount should fail with bad superblock"
verify_not_mounted $DEV $MPT "should not be mounted after failed mount"
expect_good ${MKFS} $DEV           -- "clean mkfs should succeed"

# Mount without specifying fuse or otherwise; famfs should figure it out
expect_good ${CLI} mount -v $MOUNT_OPTS $DEV $MPT -- "mount with unspecified type should work"
expect_good sudo umount $MPT       -- "umount $MPT should succeed"
verify_not_mounted $DEV $MPT "umount failed?"

expect_good ${MOUNT} $DEV $MPT     -- "mount of clean file system should succeed"
expect_fail ${MOUNT} $DEV $MPT     -- "Double mount should fail "

verify_mounted $DEV $MPT "mount failed?"

set +x
echo ":==*************************************************************************"
echo ":==prepare completed successfully"
echo ":==*************************************************************************"
exit 0
