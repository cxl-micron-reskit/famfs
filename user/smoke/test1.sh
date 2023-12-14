#!/usr/bin/env bash

cwd=$(pwd)

# Defaults running from the directory where this file lives
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
	    VG="valgrind --leak-check=full --show-leak-kinds=all"
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
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

verify_mounted $DEV $MPT "test1.sh"
sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"

${CLI} fsck $MPT && fail "fsck by path should fail when not mounted"
${CLI} fsck $DEV || fail "fsck by dev should succeed when not mounted"

full_mount $DEV $MPT "test1.sh"

${CLI} fsck $MPT || fail "fsck by path should succeed when mounted"

# Try exclusive open in driver to make this test good;
# (currently succeeds but it should fail)
${CLI} fsck $DEV && fail "fsck by dev should fail when mounted"

verify_mounted $DEV $MPT "test1.sh"

#
# Do stuff with files bigger than a page, cautiously
#

F=test10
${CLI} creat -r -s 8192 -S 10 $MPT/$F   || fail "creat $F"
${CLI} verify -S 10 -f $MPT/$F || fail "verify $F after replay"

F=bigtest0
${CLI} creat -v -r -S 42 -s 0x800000 $MPT/$F   || fail "creat $F"
${CLI} verify -S 42 -f $MPT/$F                 || fail "$F mismatch"

${CLI} cp $MPT/$F $MPT/${F}_cp      || fail "cp $F"
${CLI} verify -S 42 -f $MPT/${F}_cp || fail "verify ${F}_cp"

${CLI} mkdir $MPT/subdir || fail "failed to create subdir"
${CLI} mkdir $MPT/subdir && fail "creating existing subdir should fail"
${CLI} mkdir $MPT/$F && fail "mkdir that collides with existing file should fail"


${CLI} cp $MPT/$F $MPT/subdir/${F}_cp0      || fail "cp0 $F"
${CLI} cp $MPT/$F $MPT/subdir/${F}_cp1      || fail "cp1 $F"
${CLI} cp $MPT/$F $MPT/subdir/${F}_cp2      || fail "cp2 $F"
${CLI} cp $MPT/$F $MPT/subdir/${F}_cp3      || fail "cp3 $F"
${CLI} cp $MPT/$F $MPT/subdir/${F}_cp4      || fail "cp4 $F"
${CLI} cp $MPT/$F $MPT/subdir/${F}_cp5      || fail "cp5 $F"
${CLI} cp $MPT/$F $MPT/subdir/${F}_cp6      || fail "cp6 $F"
${CLI} cp $MPT/$F $MPT/subdir/${F}_cp7      || fail "cp7 $F"
${CLI} cp -v $MPT/$F $MPT/subdir/${F}_cp8      || fail "cp8 $F"
${CLI} cp -v $MPT/$F $MPT/subdir/${F}_cp9      || fail "cp9 $F"

${CLI} logplay -n $MPT

${CLI} verify -S 42 -f $MPT/subdir/${F}_cp0 || fail "verify ${F}_cp0"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp1 || fail "verify ${F}_cp1"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp2 || fail "verify ${F}_cp2"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp3 || fail "verify ${F}_cp3"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp4 || fail "verify ${F}_cp4"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp5 || fail "verify ${F}_cp5"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp6 || fail "verify ${F}_cp6"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp7 || fail "verify ${F}_cp7"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp8 || fail "verify ${F}_cp8"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp9 || fail "verify ${F}_cp9"

sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

${CLI} verify -S 42 -f $MPT/${F}_cp || fail "verify ${F}_cp"

${CLI} verify -S 42 -f $MPT/subdir/${F}_cp0 || fail "verify ${F}_cp0"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp1 || fail "verify ${F}_cp1"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp2 || fail "verify ${F}_cp2"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp3 || fail "verify ${F}_cp3"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp4 || fail "verify ${F}_cp4"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp5 || fail "verify ${F}_cp5"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp6 || fail "verify ${F}_cp6"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp7 || fail "verify ${F}_cp7"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp8 || fail "verify ${F}_cp8"
${CLI} verify -S 42 -f $MPT/subdir/${F}_cp9 || fail "verify ${F}_cp9"

${CLI} fsck $MPT || fail "fsck should succeed"
${CLI} fsck -v $MPT || fail "fsck should succeed"

set +x
echo "*************************************************************************************"
echo "Test1 completed successfully"
echo "*************************************************************************************"
