  usr/bin/bash
 
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
MPT=/mnt/tagfs

CLI="sudo debug/tagfs"

set -x

verify_mounted $DEV $MPT "test2.sh"
${CLI} fsck $MPT || fail "fsck should succeed"

# Try to create a file that is not in a tagfs file system (assume relative path not in one)
NOT_IN_TAGFS=no_leading_slash
${CLI} creat -s 0x400000 -f $NOT_IN_TAGFS \
     && fail "creating file not in tagfs file system should fail"

# Tagfs getmap should succeed on a file that exists
LOG=$MPT/.meta/.log
${CLI} getmap $LOG || fail "getmap should succeed on the tagfs log file"


# tagfs getmap should fail on a file that does not exist
NOTEXIST=$MPT/not_exist
${CLI} getmap $NOT_EXIST && fail "getmap should fail non nonexistent file in tagfs"

# tagfs getmap should fail on a file that is not in a tagfs file system
${CLI} getmap $NOT_IN_TAGFS && fail "getmap should fail if file not in tagfs"


F=bigtest
SIZE=0x4000000
for N in 10 11 12 13 14 15 16
do
    FILE=${F}${N}
    ${CLI} creat -r -S $N -s $SIZE -f $MPT/$FILE || fail "creat $FILE"
    ${CLI} verify -S $N -f $MPT/$FILE                || fail "$FILE mismatch"
done

for N in 10 11 12 13 14 15 16
do
    FILE=${F}${N}
    ${CLI} verify -S $N -f $MPT/$FILE                || fail "$FILE mismatch"
done

${CLI} fsck $MPT || fail "fsck should not fail when nothing cloned"

${CLI} clone $MPT/${FILE} $MPT/${FILE}_clone         || fail "clone $F "
${CLI} clone $MPT/${FILE} $MPT/${FILE}_clone1        || fail "clone $F "

${CLI} fsck $MPT && fail "fsck should fail after cloning "
${CLI} verify -S $N -f $MPT/${FILE}_clone  || fail "${FILE}_clone mismatch"
${CLI} verify -S $N -f $MPT/${FILE}_clone1 || fail "${FILE}_clone1 mismatch"

sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

# TODO: make logplay drop files that have allocation collisions
#test -f $MPT/bigtest0_clone && fail "cloned file should disappear after remount"

${CLI} fsck $MPT && fail "fsck if a clone has ever happened should fail"


set +x
echo "*************************************************************************************"
echo "Test2 completed successfully"
echo "*************************************************************************************"
