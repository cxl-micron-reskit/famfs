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
${CLI} fsck $MPT || fail "fsck should succeed"

# Try to create a file that is not in a famfs file system (assume relative path not in one)
NOT_IN_FAMFS=no_leading_slash
${CLI} creat -s 0x400000 $NOT_IN_FAMFS \
     && fail "creating file not in famfs file system should fail"

# Famfs getmap should succeed on a file that exists
LOG=$MPT/.meta/.log
${CLI} getmap $LOG || fail "getmap should succeed on the famfs log file"


# famfs getmap should fail on a file that does not exist
NOTEXIST=$MPT/not_exist
${CLI} getmap $NOT_EXIST && fail "getmap should fail non nonexistent file in famfs"

# famfs getmap should fail on a file that is not in a famfs file system
${CLI} getmap $NOT_IN_FAMFS && fail "getmap should fail if file not in famfs"


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

sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "$MOUNT_OPTS" "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

set +x
echo "*************************************************************************************"
echo "Test2 completed successfully"
echo "*************************************************************************************"
