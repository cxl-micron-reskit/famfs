#/usr/bin/bash

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

set -x

sudo mkdir -p $MPT || fail "mkdir"

# Make sure tagfs is not currently mounted
grep -c tagfs /proc/mounts         && fail "tagfs is currently mounted"

# destroy tagfs file system, if any
sudo debug/mkfs.tagfs -f -k $DEV    || fail "mkfs/kill"
sudo debug/mkfs.tagfs  $DEV         || fail "mkfs"
sudo debug/mkfs.tagfs  $DEV         && fail "mkfs redo" # fail, fs exists

#debug/tagfs mkmeta /dev/pmem0                  || fail "mkmeta"
sudo debug/tagfs fsck $DEV          || fail "fsck"

sudo insmod ../kmod/tagfs.ko       || fail "insmod"
sudo mount -t tagfs -o noatime -o dax=always -o rootdev=/dev/pmem0 /dev/pmem0 $MPT \
    || fail "mount"

grep tagfs /proc/mounts             || fail "mount appears to have failed"

sudo debug/tagfs mkmeta $DEV        || fail "mkmeta"
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "no log file after mkmeta"

# Create 1 file and verify
sudo debug/tagfs creat -r -s 4096 -S 1 -f $MPT/test1   || fail "creat test1"
sudo debug/tagfs verify -S 1 -f $MPT/test1 || fail "verify 1 after creat"

# Create 2 more files
sudo debug/tagfs creat -r -s 4096 -S 2 -f $MPT/test2   || fail "creat test2"
sudo debug/tagfs creat -r -s 4096 -S 3 -f $MPT/test3   || fail "creat test3"

# Verify all 3 files
sudo debug/tagfs verify -S 1 -f $MPT/test1 || fail "verify 1 after multi creat"
sudo debug/tagfs verify -S 2 -f $MPT/test2 || fail "verify 2 after multi creat"
sudo debug/tagfs verify -S 3 -f $MPT/test3 || fail "verify 3 after multi creat"

# Unmount and remount
sudo umount $MPT || fail "umount"
grep -c tagfs /proc/mounts         && fail "tagfs is still mounted after umount attempt"

sudo mount -t tagfs -o noatime -o dax=always -o rootdev=/dev/pmem0 /dev/pmem0 $MPT \
     || fail "mount"

grep -c tagfs /proc/mounts         || fail "tagfs not mounted after remount attempt"


echo "this logplay should fail because we haven't done mkmeta yet"
sudo debug/tagfs logplay $MPT               && fail "logplay 1"   

# Post mount, re-create the meta files
sudo debug/tagfs mkmeta $DEV                || fail "mkmeta 2"
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "no log file after mkmeta"

sudo ls -lR $MPT
sudo debug/tagfs mkmeta $DEV               || fail "mkmeta repeat should fail"

# Relay the log, recovering the files that existed befure the umount
sudo debug/tagfs logplay  $MPT             || fail "logplay 2" # should fail till mkmeta

# Re-verify the files from prior to the umount
sudo debug/tagfs verify -S 1 -f $MPT/test1 || fail "verify test1 after replay"
sudo debug/tagfs verify -S 2 -f $MPT/test2 || fail "verify test2 after replay"
sudo debug/tagfs verify -S 3 -f $MPT/test3 || fail "verify test3 after replay"

set +x
echo "*************************************************************************************"
echo "Test0 completed successfully"
echo "*************************************************************************************"



