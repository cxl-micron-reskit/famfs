#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=../scripts/
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
RMMOD=0

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
	(-n|--no-rmmod)
	    RMMOD=0
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

echo "DEVTYPE=$DEVTYPE"
CLI="sudo $VG $BIN/famfs"
MULTICHASE="sudo $BIN/src/multichase/multichase"
TEST="test4"F
FUSED="sudo $VG $BIN/famfs_fused"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Start with a clean, empty file systeem
famfs_recreate "test4"

verify_mounted $DEV $MPT "test4.sh"

${CLI} badarg                            && fail "create badarg should fail"
${CLI} creat  -h                         || fail "creat -h should succeed"
${CLI} creat -s 3g  ${MPT}/memfile       || fail "can't create memfile for multichase"
${CLI} creat -s 100m ${MPT}/memfile1     || fail "creat should succeed with -s 100m"
${CLI} creat -s 10000k ${MPT}/memfile2   || fail "creat with -s 10000k should succeed"

${MULTICHASE} -d ${MPT}/memfile -m 2900m || fail "multichase fail"

verify_mounted $DEV $MPT "test4.sh mounted"
sudo $UMOUNT $MPT || fail "test4.sh umount"
verify_not_mounted $DEV $MPT "test4.sh"

# Test shadow logplay while the fs is not mounted
SHADOWPATH=/tmp/shadowpath
${CLI} logplay --shadow -d /dev/bogodax && fail "shadow logplay should fail with bogus daxdev"
sudo rm -rf $SHADOWPATH
${CLI} logplay --shadow $SHADOWPATH/frob --daxdev $DEV -vv && \
    fail "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"
${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv  || \
    fail "shadow logplay to nonexistent shadow dir should succeed if parent exists"
${CLI} logplay --daxdev $DEV -vv  $SHADOWPATH && \
    fail "logplay should fail if --daxdev is set without --shadow"

sudo rm -rf $SHADOWPATH
sudo mkdir -p $SHADOWPATH
${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv || \
    fail "shadow logplay to existing shadow dir should succeed"
${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv || \
    fail "redo shadow logplay to existing shadow dir should succeed"

# Double shadow arg means re-parse yaml to test (if the shadow files are not already present)
sudo rm -rf $SHADOWPATH
${CLI} logplay --shadow $SHADOWPATH --shadowtest --daxdev $DEV  -vv || \
    fail "shadow logplay with yaml test to existing shadow dir should succeed"

${CLI} logplay --shadow $SHADOWPATH --shadow $SHADOWPATH --daxdev $DEV  -vv && \
    fail "shadow logplay with yaml test with duplicate shadowpaths should fail"

#TODO: add some bad yaml to the yaml tree to test failures (or maybe do this in unit tests?

#
# Test cli 'famfs mount'
#
# Second mount causes fubar on 6.7, but fails as it should on 6.5 TODO: fix it!!
${CLI} mount -vvv $DEV $MPT || fail "famfs mount should succeed when not mounted"
${CLI} mount -vvv $DEV $MPT 2>/dev/null && fail "famfs mount should fail when already mounted"

verify_mounted $DEV $MPT "test4.sh remount"

${CLI} logplay --shadow $SHADOWPATH --shadowtest $MPT  -vv || \
    fail "shadow logplay from mounted meta files should succeed"

# check that a removed file is restored on remount
F="$MPT/test_xfile"
${CLI} creat -s 16m -r -S 42 $F || fail "failed to create F ($F)"
sudo rm $F
sudo $UMOUNT $MPT            || fail "umount failed"

verify_not_mounted $DEV $MPT "test4.sh 2nd umount"


${CLI} mount -?             || fail "famfs mount -? should succeed"
${CLI} mount                && fail "famfs mount with no args should fail"
${CLI} mount  a b c         && fail "famfs mount with too many args should fail"
${CLI} mount baddev $MPT    && fail "famfs mount with bad device path should fail"
${CLI} mount $DEV badmpt    && fail "famfs mount with bad mount point path should fail"


verify_not_mounted $DEV $MPT "test4.sh various bad mount attempts"

${CLI} mount -rm -vvv $DEV $MPT && fail "famfs mount with -r and -m should fail"
${CLI} mount -r -vvv $DEV $MPT  || fail "famfs mount 2 should succeed when not mounted"
verify_mounted $DEV $MPT "test4.sh 2nd remount"

sudo test -f $F             || fail "bogusly deleted file did not reappear on remount"
${CLI} verify -S 42 -f $F
sudo $UMOUNT $MPT            || fail "umount should succeed"

if ((RMMOD > 0)); then
    sudo rmmod famfs            || fail "could not unload famfs when unmoounted"
    ${CLI} mount -vvv $DEV $MPT && fail "famfs mount should fail when kmod not loaded"
fi
sudo modprobe famfs         || fail "modprobe"
${CLI} mount $DEV $MPT      || fail "famfs mount should succeed after kmod reloaded"

#TODO troubleshoot remount
${CLI} mount -R $DEV $MPT   || fail "famfs mount -R should succeed when nothing is hinky"
# mount -R needs mkmeta cleanup...

SHADOW_TARGET=~/smoke.shadow
THIS_SHADOW=test4.shadow
SH=${SHADOW_TARGET}/${THIS_SHADOW}
mkdir -p ${SHADOW_TARGET}
rm -rf $SH
${CLI} logplay --shadow $SH $MPT

sudo $UMOUNT $MPT # run_smoke.sh expects the file system unmounted after this test


# This stuff may go away after all commands work via fuse
${FUSED} -s -o shadow="$SH" -o daxdev=/dev/dax1.0 $MPT || fail "fuse mount on $MPT"
sudo ls -al $MPT || fail "can't list directory via fuse mount"
${CLI} verify -S 42 -f $MPT/test_xfile || fail "bad verify via fuse mount"
${CLI} fsck $MPT || fail "fsck of fuse mount should work"
sudo umount $MPT

#sudo trace-cmd record -p function -l 'fuse*' -l 'famfs*' umount $MPT
#sudo trace-cmd report > umount-trace.txt

set +x
echo "*************************************************************************"
echo "test4 (multichase) completed successfully"
echo "*************************************************************************"
exit 0
