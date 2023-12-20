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

verify_mounted $DEV $MPT "test2.sh"

sudo cmp $MPT/bigtest0 $MPT/bigtest0_cp       || fail "copies should match"
sudo cmp $MPT/bigtest10 $MPT/bigtest11        && fail "files should not match"

${CLI} creat -r -s 4096 -S 1 $MPT/ddtest   || fail "creat ddfile"
${CLI} verify -S 1 -f $MPT/test1              || fail "verify ddfile creat"
#sudo dd if=/dev/zero of=$MPT/ddtest bs=4096 conv=notrunc  || fail "dd into ddfile"
#${CLI} verify -S 1 -f $MPT/test1              && fail "verify should fail after dd overwrite"
sudo dd of=/dev/null if=$MPT/ddtest bs=4096   || fail "dd out of ddfile"



sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "${MOUNT_OPTS}"  "full_mount"
verify_mounted $DEV $MPT "test1.sh"

${CLI} fsck $MPT || fail "fsck should succeed - no cross links yet"


set +x
echo "*************************************************************************************"
echo "Test3 completed successfully"
echo "*************************************************************************************"
