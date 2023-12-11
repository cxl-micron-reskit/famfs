#!/usr/bin/env bash

 
fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

cwd=$(pwd)
export PATH=cwd/debug:$PATH

DEV=/dev/pmem0
MPT=/mnt/famfs

# Run under valgrind?
VG=""
#VG="valgrind --leak-check=full --show-leak-kinds=all"

CLI="sudo $VG debug/famfs"

source test_funcs.sh
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
full_mount $DEV $MPT "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

# TODO: make logplay drop files that have allocation collisions (?)
#test -f $MPT/bigtest0_clone && fail "cloned file should disappear after remount"

set +x
echo "*************************************************************************************"
echo "Test2 completed successfully"
echo "*************************************************************************************"
