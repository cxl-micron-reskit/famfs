#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
DEV="/dev/pmem0"
VG=""
SCRIPTS=../scripts
MPT=/mnt/famfs
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
BIN=../debug
KMOD=../../kmod

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
	(-k|--kmod)
	    KMOD=$1
	    shift
	    ;;
	(-v|--valgrind)
	    # no argument to -v; just setup for Valgrind
	    VG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
	    ;;
	*)
	    remainder="$flag $1";
	    shift;
	    while (( $# > 0)); do
		remainder="$remainder $1"
		shift
	    done
	    echo "ignoring commandline remainder: $remainder"
	    ;;

    esac
done

echo "DEVTYPE=$DEVTYPE"
echo "SCRIPTS=$SCRIPTS"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

full_mount $DEV $MPT "${MOUNT_OPTS}" "test_errors full_mount"

verify_mounted $DEV $MPT "test2.sh"

${CLI} fsck $MPT || fail "fsck should not fail when nothing cloned"

N="10"
FILE="bigtest$N"
${CLI} clone -h                                      || fail "clone -h should succeed"
${CLI} clone $MPT/${FILE} $MPT/${FILE}_clone         || fail "clone $F "
${CLI} clone -v $MPT/${FILE} $MPT/${FILE}_clone1        || fail "clone $F "

# Error cases
${CLI} clone -v $MPT/bogusfile $MPT/bogusfile.cllone && fail "clone bogusfile should fail"
${CLI} clone -v /etc/passwd $MPT/passwd              && fail "clone from outside famfs should fail"


${CLI} fsck $MPT && fail "fsck should fail after cloning "
${CLI} verify -S $N -f $MPT/${FILE}_clone  || fail "${FILE}_clone mismatch"
${CLI} verify -S $N -f $MPT/${FILE}_clone1 || fail "${FILE}_clone1 mismatch"

sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "${MOUNT_OPTS}" "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

${CLI} fsck -v $MPT && fail "fsck -v if a clone has ever happened should fail"
${CLI} fsck $MPT && fail "fsck if a clone has ever happened should fail"

sudo umount $MPT || fail "umount"

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
