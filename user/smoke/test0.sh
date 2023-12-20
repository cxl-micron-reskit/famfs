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

MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

sudo mkdir -p $MPT || fail "mkdir"

# Make sure famfs is not currently mounted
grep -c famfs /proc/mounts         && fail "famfs is currently mounted"

# destroy famfs file system, if any
${MKFS} -f -k $DEV    || fail "mkfs/kill"
${MKFS}  $DEV         || fail "mkfs"
${MKFS}  $DEV         && fail "mkfs redo" # fail, fs exists

#debug/famfs mkmeta /dev/pmem0                  || fail "mkmeta"
${CLI} fsck $DEV          || fail "fsck"

sudo insmod $KMOD/famfs.ko       || fail "insmod"

sudo mount $MOUNT_OPTS $DEV $MPT || fail "mount"
#sudo mount $MOUNT_OPTS $DEV $MPT && fail "double mount should fail"

grep famfs /proc/mounts             || fail "No famfs mounted"
grep $DEV /proc/mounts              || fail "dev=$DEV not in /proc/mounts~"
grep $MPT /proc/mounts              || fail "Mount pt $MPT not in /proc/mounts~"

${CLI} mkmeta $DEV        || fail "mkmeta"
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "no log file after mkmeta"

# Create 1 file and verify
${CLI} creat -r -s 4096 -S 1 $MPT/test1   || fail "creat test1"
${CLI} verify -S 1 -f $MPT/test1 || fail "verify 1 after creat"

# Create 2 more files
${CLI} creat -r -s 4096 -S 2 $MPT/test2   || fail "creat test2"
${CLI} creat -r -s 4096 -S 3 $MPT/test3   || fail "creat test3"

# Verify all 3 files
${CLI} verify -S 1 -f $MPT/test1 || fail "verify 1 after multi creat"
${CLI} verify -S 2 -f $MPT/test2 || fail "verify 2 after multi creat"
${CLI} verify -S 3 -f $MPT/test3 || fail "verify 3 after multi creat"

# Create same file should fail
${CLI} creat -r -s 4096 -S 1 $MPT/test1   && fail "Create should fail if file exists"

# Unmount and remount
sudo umount $MPT || fail "umount"
grep -c famfs /proc/mounts         && fail "famfs is still mounted after umount attempt"

sudo mount $MOUNT_OPTS $DEV $MPT   || fail "mount"

grep -c famfs /proc/mounts         || fail "famfs not mounted after remount attempt"

echo "this logplay should fail because we haven't done mkmeta yet"
${CLI} logplay -v $MPT               && fail "logplay 1 before mkmeta"

# Post mount, re-create the meta files
${CLI} mkmeta $DEV                || fail "mkmeta 2"
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "no log file after mkmeta"

sudo ls -lR $MPT
${CLI} logplay -v $MPT             || fail "logplay after mkmeta should work"
${CLI} mkmeta $DEV               || fail "mkmeta repeat should fail"

# Replay the log, recovering the files that existed befure the umount
${CLI} logplay -v $MPT             || fail "logplay 3 should work but be nop"

# Re-verify the files from prior to the umount
${CLI} verify -S 1 -f $MPT/test1 || fail "verify test1 after replay"
${CLI} verify -S 2 -f $MPT/test2 || fail "verify test2 after replay"
${CLI} verify -S 3 -f $MPT/test3 || fail "verify test3 after replay"

${CLI} fsck $MPT || fail "fsck should succeed"

set +x
echo "*************************************************************************************"
echo "Test0 completed successfully"
echo "*************************************************************************************"



