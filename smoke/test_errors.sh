#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=../scripts
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
BIN=../debug

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
	    VG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

echo "DEVTYPE=$DEVTYPE"
echo "SCRIPTS=$SCRIPTS"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
TEST="test_errors:"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

#full_mount $DEV $MPT "${MOUNT_OPTS}" "test_errors full_mount"

verify_mounted $DEV $MPT "test_errors.sh"

${CLI} fsck $MPT || fail "fsck should not fail when nothing cloned"

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


${CLI} fsck $MPT && fail "fsck should fail after cloning "
${CLI} verify -S 10 -f $MPT/clone  || fail "re-verify clone"
${CLI} verify -S 10 -f $MPT/clone1 || fail "re-verify clone1"

sudo $UMOUNT $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "${MOUNT_OPTS}" "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

# Throw a curveball or two at logplay
${CLI} mkdir $MPT/adir         || fail "should be able to create adir"
sudo rm -rf $MPT/adir          || fail "should be able to rogue remove a dir"
sudo touch $MPT/adir           || fail "should be able to create rogue file"
sudo rm -rf $MPT/adir2         || fail "should be able to rogue remove a dir2"
sudo ln -s /tmp $MPT/adir2     && fail "symlink should fail with famfs kmod v2"
sudo touch $MPT/adir2          || fail "should be able to touch a file"
${CLI} logplay $MPT && fail "logplay should complain when a file is where a dir should be"

${CLI} fsck -v $MPT && fail "fsck -v if a clone has ever happened should fail"
${CLI} fsck $MPT && fail "fsck if a clone has ever happened should fail"

# Some v2-specific tests

sudo $UMOUNT $MPT            || fail "umount should work"
${CLI} mount $DEV $MPT       || fail "basic mount should succeed"
${CLI} mount $DEV $MPT       && fail "remount 1 should fail"
${CLI} mount $DEV $MPT       && fail "remount 2 should fail"
${CLI} mount $DEV $MPT       && fail "remount 3 should fail"
sudo mkdir -p /tmp/famfs
${CLI} mount $DEV /tmp/famfs && fail "remount at different path should fail"
verify_mounted $DEV $MPT     "mounted after redundant mounts"
sudo $UMOUNT $MPT            || fail "umount should work after redundant mounts"
verify_not_mounted $DEV $MPT "umount should have worked after redundant mounts"

${CLI} mount $DEV $MPT       || fail "basic mount should succeed"

#sudo $UMOUNT $MPT || fail "umount"

set +x
echo "*************************************************************************************"
echo " Important note: This test (at least the first run) will generate a stack dump"
echo " in the kernel log (a WARN_ONCE) due to cross-linked pages (specifically DAX noticing"
echo " that a page was mapped to more than one file. This is normal, as this test intentionally"
echo " does bogus cross-linked mappings"
echo "*************************************************************************************"
echo "Test_errors completed successfully"
echo "*************************************************************************************"
exit 0
