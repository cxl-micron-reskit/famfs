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
FSCK="${CLI} fsck $FSCK_OPTS"
TEST="test_errors:"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

if [[ "${FAMFS_MODE}" == "fuse" ]]; then
    echo "*************************************************************"
    echo "test_errs does not support famfs/fuse yet"
    echo "*************************************************************"
    sleep 1
    exit 0
fi

set -x

# Start with a clean, empty file system
famfs_recreate "test_errors"

verify_mounted $DEV $MPT "test_errors.sh"

${FSCK} $MPT || fail "fsck should not fail when nothing cloned"

# Create a file to clone
${CLI} creat -r -S 10 -s 0x400000 $MPT/original || fail "create original should succeed"
${CLI} verify -S 10 -f $MPT/original            || fail "verify original should succeed"

N="10"
FILE="bigtest$N"
${CLI} clone -h                               || fail "clone -h should succeed"
${CLI} clone $MPT/original $MPT/clone         || fail "clone original should succeed"
${CLI} clone -v $MPT/original $MPT/clone1     || fail "second clone should succeed"
${CLI} clone -v $MPT/clone $MPT/clone2        || fail "clone from clone should also succeed"

${CLI} verify -S 10 -f $MPT/clone             || fail "verify clone should succeed"
${CLI} verify -S 10 -f $MPT/clone1            || fail "verify clone1 should succeed"
${CLI} verify -S 10 -f $MPT/clone2            || fail "verify clone2 should succeed"


# Error cases
${CLI} clone  && fail "clone with no args should fail"
${CLI} clone -v $MPT/bogusfile $MPT/bogusfile.cllone && fail "clone bogusfile should fail"
${CLI} clone -v /etc/passwd $MPT/passwd              && fail "clone from outside famfs should fail"


${FSCK} $MPT && fail "fsck should fail after cloning "
${CLI} verify -S 10 -f $MPT/clone  || fail "re-verify clone"
${CLI} verify -S 10 -f $MPT/clone1 || fail "re-verify clone1"

sudo $UMOUNT $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test_errors .sh"
${MOUNT} $DEV $MPT || fail "mount in test_errors"
#full_mount $DEV $MPT "${MOUNT_OPTS}" "test_errors.sh"
verify_mounted $DEV $MPT "test1.sh"

# Throw a curveball or two at logplay
${CLI} mkdir $MPT/adir         || fail "should be able to mkdir adir"
${CLI} mkdir $MPT/adir2        || fail "should be able to mkdir adir2"
sudo rm -rf $MPT/adir
if (( $? == 0 )); then
    sudo touch $MPT/adir           || fail "should be able to create rogue file"
    sudo rm -rf $MPT/adir2         \
	|| fail "should be able to rogue remove a dir2"
    sudo touch $MPT/adir2          || fail "should be able to touch a file"
    ${CLI} logplay -vvv $MPT       \
	&& fail "logplay should complain when a file is where a dir should be"
fi
sudo ln -s /tmp $MPT/adir2     && fail "symlink should fail with famfs kmod v2"

${FSCK} -v $MPT && fail "fsck -v if a clone has ever happened should fail"
${FSCK} $MPT && fail "fsck if a clone has ever happened should fail"

# Some v2-specific tests

sudo $UMOUNT $MPT            || fail "umount should work"
${MOUNT} $DEV $MPT       || fail "basic mount should succeed"
${MOUNT} $DEV $MPT       && fail "remount 1 should fail"
${MOUNT} $DEV $MPT       && fail "remount 2 should fail"
${MOUNT} $DEV $MPT       && fail "remount 3 should fail"
sudo mkdir -p /tmp/famfs
${MOUNT} $DEV /tmp/famfs && fail "remount at different path should fail"
verify_mounted $DEV $MPT     "mounted after redundant mounts"
sudo $UMOUNT $MPT            || fail "umount should work after redundant mounts"
verify_not_mounted $DEV $MPT "umount should have worked after redundant mounts"

${MOUNT} $DEV $MPT       || fail "basic mount should succeed"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_errors.shadow $MPT

#sudo $UMOUNT $MPT || fail "umount"

set +x
echo "*************************************************************************"
echo " Important note: This test (at least the first run) will generate a stack dump"
echo " in the kernel log (a WARN_ONCE) due to cross-linked pages (specifically DAX noticing"
echo " that a page was mapped to more than one file. This is normal, as this test intentionally"
echo " does bogus cross-linked mappings"
set +x
echo ":==*************************************************************************"
echo ":==test_errors completed successfully"
echo ":==*************************************************************************"
exit 0
