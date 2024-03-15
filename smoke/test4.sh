#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
DEV="/dev/pmem0"
VG=""
SCRIPTS=../scripts/
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
RMMOD=0

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
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
MULTICHASE="sudo $BIN/src/multichase/multichase"
TEST="test4"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

verify_mounted $DEV $MPT "test2.sh"

${CLI} badarg                            && fail "create badarg should fail"
${CLI} creat  -h                         || fail "creat -h should succeed"
${CLI} creat -s 3g  ${MPT}/memfile       || fail "can't create memfile for multichase"
${CLI} creat -s 100m ${MPT}/memfile1     || fail "creat should succeed with -s 100m"
${CLI} creat -s 10000k ${MPT}/memfile2   || fail "creat with -s 10000k should succeed"

# Let's count the faults during the multichase run
sudo sh -c "echo 1 > /sys/fs/famfs/fault_count_enable"

${MULTICHASE} -d ${MPT}/memfile -m 2900m || fail "multichase fail"

set +x
echo -n "pte faults:"
sudo cat /sys/fs/famfs/pte_fault_ct || fail "cat pte_fault_ct"
echo
echo -n "pmd faults: "
sudo cat /sys/fs/famfs/pmd_fault_ct || fail "cat pmd_fault_ct"
echo
echo -n "pud faults: "
sudo cat /sys/fs/famfs/pud_fault_ct || fail "cat pud_fault_ct"
echo
set -x
verify_mounted $DEV $MPT "test4.sh mounted"
sudo $UMOUNT $MPT || fail "test4.sh umount"
verify_not_mounted $DEV $MPT "test4.sh"

#
# Test cli 'famfs mount'
#
# Second mount causes fubar on 6.7, but fails as it should on 6.5 TODO: fix it!!
${CLI} mount -vvv $DEV $MPT || fail "famfs mount should succeed when not mounted"
${CLI} mount -vvv $DEV $MPT 2>/dev/null && fail "famfs mount should fail when already mounted"

verify_mounted $DEV $MPT "test4.sh remount"

# check that a removed file is restored on remount
F="$MPT/test1"
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

sudo $UMOUNT $MPT # run_smoke.sh expects the file system unmounted after this test
${CLI} mount $DEV $MPT || fail "last famfs mount should succeed"
set +x
echo "*************************************************************************************"
echo "Test4 (multichase) completed successfully"
echo "*************************************************************************************"
exit 0
