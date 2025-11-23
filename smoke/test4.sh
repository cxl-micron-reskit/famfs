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
CLI_NOSUDO="$VG $BIN/famfs"
FSCK="${CLI} fsck $FSCK_OPTS"
TEST="test4"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

MULTICHASE="sudo $BIN/src/multichase/multichase"

set -x

# Start with a clean, empty file systeem
famfs_recreate "test4"

verify_mounted $DEV $MPT "test4.sh"

expect_fail ${CLI} badarg                            -- "create badarg should fail"
expect_good ${CLI} creat  -h                         -- "creat -h should succeed"
expect_good ${CLI} creat -s 3g  ${MPT}/memfile       -- "can't create memfile for multichase"
expect_good ${CLI} creat -s 100m ${MPT}/memfile1     -- "creat should succeed with -s 100m"
expect_good ${CLI} creat -s 10000k ${MPT}/memfile2   -- "creat with -s 10000k should succeed"

expect_good ${MULTICHASE} -d ${MPT}/memfile -m 2900m -- "multichase fail"

verify_mounted $DEV $MPT "test4.sh mounted"
expect_good sudo $UMOUNT $MPT -- "test4.sh umount"
verify_not_mounted $DEV $MPT "test4.sh"

# Test shadow logplay while the fs is not mounted
SHADOWPATH=/tmp/shadowpath/root
expect_fail ${CLI} logplay --shadow -d /dev/bogodax -- "shadow logplay should fail with bogus daxdev"
sudo rm -rf $SHADOWPATH
expect_fail ${CLI} logplay --shadow $SHADOWPATH/frob --daxdev $DEV -vv -- \
    "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"
expect_fail ${CLI} logplay --daxdev $DEV -vv  $SHADOWPATH -- \
    "logplay should fail if --daxdev is set without --shadow"

sudo rm -rf $SHADOWPATH
sudo mkdir -p $SHADOWPATH
expect_good ${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv -- \
    "shadow logplay to existing shadow dir should succeed"
expect_good ${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv -- \
    "redo shadow logplay to existing shadow dir should succeed"

# Double shadow arg means re-parse yaml to test (if the shadow files are not already present)
sudo rm -rf $SHADOWPATH

expect_fail ${CLI} logplay --shadow $SHADOWPATH --shadow $SHADOWPATH --daxdev $DEV  -vv -- \
    "shadow logplay with yaml test with duplicate shadowpaths should fail"

#TODO: add some bad yaml to the yaml tree to test failures (or maybe do this in unit tests?

#
# Test cli 'famfs mount'
#
# Second mount causes fubar on 6.7, but fails as it should on 6.5 TODO: fix it!!
expect_good ${MOUNT} -vvv $DEV $MPT -- "famfs mount should succeed when not mounted"
expect_fail ${MOUNT} -vvv $DEV $MPT 2>/dev/null -- "famfs mount should fail when already mounted"

verify_mounted $DEV $MPT "test4.sh remount"

sudo mkdir ${SHADOWPATH}
expect_good ${CLI} logplay --shadow $SHADOWPATH --shadowtest $MPT  -vv -- \
    "shadow logplay from mounted meta files should succeed"

# check that a removed file is restored on remount
F="$MPT/test_xfile"
expect_good ${CLI} creat -s 16m -r -S 42 $F -- "failed to create F ($F)"
sudo rm $F
expect_good sudo $UMOUNT $MPT            -- "umount failed"

verify_not_mounted $DEV $MPT "test4.sh 2nd umount"


expect_good ${MOUNT} -?             -- "famfs mount -? should succeed"
expect_fail ${MOUNT}                -- "famfs mount with no args should fail"
expect_fail ${MOUNT}  a b c         -- "famfs mount with too many args should fail"
expect_fail ${MOUNT} baddev $MPT    -- "famfs mount with bad device path should fail"
expect_fail ${MOUNT} $DEV badmpt    -- "famfs mount with bad mount point path should fail"


verify_not_mounted $DEV $MPT "test4.sh various bad mount attempts"

expect_fail ${MOUNT} -rm -vvv $DEV $MPT -- "famfs mount with -r and -m should fail"
expect_good ${MOUNT} -r -vvv $DEV $MPT  -- "famfs mount 2 should succeed when not mounted"
verify_mounted $DEV $MPT "test4.sh 2nd remount"

expect_good sudo test -f $F             -- "bogusly deleted file did not reappear on remount"
${CLI} verify -S 42 -f $F
expect_good sudo $UMOUNT $MPT            -- "umount should succeed"
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    if ((RMMOD > 0)); then
	expect_good sudo rmmod ${FAMFS_MOD}     -- "could not unload famfs when unmoounted"
	expect_fail ${MOUNT} -vvv $DEV $MPT -- "famfs mount should fail when kmod not loaded"
	expect_good sudo modprobe ${FAMFS_MOD}  -- "modprobe"
    fi
fi
expect_good ${MOUNT} -vv $DEV $MPT      -- "famfs mount should succeed after kmod reloaded"

#TODO troubleshoot remount
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    expect_good ${MOUNT} -R $DEV $MPT   -- "famfs mount -R should succeed when nothing is hinky"
fi
# mount -R needs mkmeta cleanup...

SHADOW_TARGET=~/smoke.shadow
THIS_SHADOW=test4.shadow
SH=${SHADOW_TARGET}/${THIS_SHADOW}
mkdir -p ${SHADOW_TARGET}
rm -rf $SH
${CLI} logplay --shadow $SH $MPT

sudo $UMOUNT $MPT # run_smoke.sh expects the file system unmounted after this test

set +x
echo ":==*************************************************************************"
echo ":==test4 (multichase) completed successfully"
echo ":==*************************************************************************"
exit 0
