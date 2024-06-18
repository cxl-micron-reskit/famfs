#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=../scripts
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"

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
TEST="prepare"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

sudo mkdir -p $MPT || fail "mkdir"

# Make sure famfs is not mounted
findmnt -t famfs $MPT
if (( $? == 0 )); then
    sudo umount $MPT
fi

# destroy famfs file system, if any
${MKFS} -h            || fail "mkfs -h should work"
${MKFS}               && fail "mkfs without dev argument should fail"
${MKFS} /tmp/nonexistent && fail "mkfs on nonexistent dev should fail"
${MKFS} -f -k $DEV    || fail "mkfs/kill"
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

# We now expect the module to already be loaded, but no harm in modprobe to make double sure
sudo modprobe famfs       || fail "modprobe"

sudo mount $MOUNT_OPTS $DEV $MPT || fail "mount"
sudo mount $MOUNT_OPTS $DEV $MPT && fail "double mount should fail"

grep famfs /proc/mounts             || fail "No famfs mounted"
grep $DEV /proc/mounts              || fail "dev=$DEV not in /proc/mounts~"
grep $MPT /proc/mounts              || fail "Mount pt $MPT not in /proc/mounts~"

${CLI} mkmeta $DEV        || fail "mkmeta"
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "prep: no log file after mkmeta"

${CLI} fsck --human $MPT || fail "prep: fsck --human should succeed"

set +x
echo "*************************************************************************"
echo "prepare.sh completed successfully"
echo "*************************************************************************"
exit 0
