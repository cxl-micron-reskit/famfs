#!/usr/bin/env bash

source smoke/test_header.sh

TEST="test0"

source $SCRIPTS/test_funcs.sh

#set -x

# Start with a clean, empty file systeem
famfs_recreate  "test0"

expect_good grep famfs /proc/mounts      -- "No famfs mounted"
expect_good grep $DEV /proc/mounts       -- "dev=$DEV not in /proc/mounts~"
expect_good grep $MPT /proc/mounts       -- "Mount pt $MPT not in /proc/mounts~"

expect_good sudo test -f $MPT/.meta/.superblock -- "no superblock file after mkmeta"
expect_good sudo test -f $MPT/.meta/.log -- "no log file after mkmeta"

# Basic CLI stuff
expect_fail ${CLI} -- "cli with no subcommand should fail"
expect_good ${CLI} -h -- "famfs -h should work"
expect_good ${CLI} -h mount -- "famfs -h mount should work"

# Create 1 file and verify
expect_good ${CLI} creat -h                 -- "creat -h should succeed"
expect_fail ${CLI} creat                    -- "creat with no args should fail"
expect_fail ${CLI} creat -r -S 1 $MPT/test1 -- "creat without size should fail"
expect_fail ${CLI} creat -S -s 10 $MPT/badf -- "creat with -S but no -r should fail"
expect_good ${CLI} creat -r -s 4096 -S 1 $MPT/test1   -- "creat test1"
expect_fail ${CLI} creat $MPT/.meta  -- "creat an existing directory should fail"
expect_fail ${CLI} creat -S 1 -r -m "$MPT/zork,4K,1" -- "multi and single mode should fail"
expect_fail ${CLI} creat -t 1000 -m "$MPT/zork,4K,1" -- "threadct=1000 should fail"
expect_fail ${CLI} creat -v -s 4K "/tmp/notfamfs" -- "multi-create not in famfs"
expect_fail ${CLI} creat -v --multi "$MPT/goodfile,4k" \
       --multi "/tmp/badfile,4M"      -- "one good, one bad file should be err"
expect_good sudo test -f "$MPT/goodfile" -- "...but the good file should be created"
expect_fail ${CLI} creat -S 1 -r -s 2048 "$MPT/goodfile"\
                                       -- "randomize with wrong size should fail"
expect_good ${CLI} creat -S 1 -r "$MPT/goodfile"  -- "randomize without size should work"
expect_fail ${CLI} creat -t 1000 --multi "$MPT/goodfile,2K,1" \
                                  -- "randomize with bad threadct should fail"
expect_fail ${CLI} creat -t 0 --multi "$MPT/goodfile,2K,1" \
                                  -- "randomize with bad threadct should fail 2"
expect_fail ${CLI} creat -t -1 "$MPT/goodfile,2K,1" \
                                  -- "randomize with bad threadct should fail 3"
expect_fail ${CLI} creat -S 1 -r -M "$MPT/notcreated,22" -- "seed and multi incompatible"
expect_fail ${CLI} creat -M "$MPT/notcreated" -- "multi with no size should fail"
expect_fail ${CLI} creat -M "$MPT/notcreated" -- "multi with no size should fail"

if [[ "$FAMFS_MODE" == "fuse" ]]; then
    exit 1
    # next test hangs on fuse..
fi

expect_fail ${CLI} creat -M "$MPT/notcr,22,22,22" -- "multi with too many params should fail"
expect_good ${CLI} creat -M "$MPT/seeded,2M,42" \
       -M "$MPT/notseeded,2M" -- "multi-create partially seeded should work"
expect_good ${CLI} verify --multi "$MPT/seeded,42" -- "seeded file should verify"

expect_good ${CLI} verify -h              -- "verify -h should succeed"
expect_fail ${CLI} verify                 -- "verify with no args should fail"
expect_fail ${CLI} verify -f $MPT/test1   -- "verify with no seed should fail"
expect_fail ${CLI} verify -S 1 -f badfile -- "verify with bad filename should fail"
expect_good ${CLI} verify -S 1 -f $MPT/test1 -- "verify 1 after creat"
expect_fail ${CLI} verify -S 99 -f $MPT/test1 -- "verify with wrong seed shoud fail"


# Create 2 more files
expect_good ${CLI} creat -r -s 4096 -S 2 $MPT/test2   -- "creat test2"
expect_good ${CLI} creat -r -s 4096 -S 3 $MPT/test3   -- "creat test3"

# Verify all 3 files
expect_good ${CLI} verify -S 1 -f $MPT/test1 -- "verify 1 after multi creat"
expect_good ${CLI} verify -S 2 -f $MPT/test2 -- "verify 2 after multi creat"
expect_good ${CLI} verify --quiet -S 3 -f $MPT/test3 -- "verify 3 after multi creat"

expect_fail ${CLI} verify -S 99 -f $MPT/test1 -- "verify with bad seed should fail"
expect_good ${CLI} verify -m "$MPT/test1,1" -- "verify --multi should succeed"
expect_fail ${CLI} verify -m "$MPT/test1,2" -- "verify --multi w/bad seed should fail"

expect_fail ${CLI} verify --multi "$MPT/test1" -- "no comma in verify multi arg"
expect_fail ${CLI} verify --multi "$MPT/test1,1,extra" -- "extra fields in multi"
expect_good ${CLI} verify --multi "$MPT/test1,1" \
       --multi "$MPT/test2,2" \
       --multi "$MPT/test3,3" \
    -- "verify multi with good seeds should succeed"
expect_fail ${CLI} verify --multi "$MPT/test1,9" \
       --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    -- "verify multi with any bad seeds should fail"
#bad thread counts
expect_fail ${CLI} verify --threadct -1 \
       --multi "$MPT/test1,1" --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    -- "verify multi with good seeds and -1 thread should fail"
expect_fail ${CLI} verify --threadct 257 \
       --multi "$MPT/test1,1" --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    -- "verify multi with good seeds and 257 thread should fail"
expect_fail ${CLI} verify -t 999 \
       --multi "$MPT/test1,9" --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    -- "verify multi with any bad seeds and bad threadct fail"

# Create same file should fail unless we're randomizing it
expect_good ${CLI} creat -r -s 4096 -S 99 $MPT/test1 -- "Create to re-init existing file should succeed"
expect_good ${CLI} creat -s 4096 $MPT/test1  -- "Recreate with same size should succeed"
expect_fail ${CLI} creat -s 8192 $MPT/test1  -- "Recreate with different size should fail"

# Put back data for seed=1 because we're gonna check it later
expect_good ${CLI} creat -r -s 4096 -S 1 $MPT/test1  -- "Create to re-init existing file should succeed"

# Create outside famfs should fail
expect_fail ${CLI} creat -r -s 4096 -S 1 /tmp/test1   -- "Create should fail if file exists"

# Create an empty file should fail
expect_fail ${CLI} creat -r -s 0 -S 1 $MPT/emptyfile  -- "Create empty file should fail"

# Test creat mode/uid/gid options
# These permissions should make it work without sudo
FMODE="600"
uid=$(id -u)
gid=$(id -g)
expect_good ${CLI} creat -s 0x100000 -r -m $FMODE -u $uid -g $gid $MPT/testmode0 -- "creat with mode/uid/gid"

#
# Check creat with the custom mode/uid/gid
#
FMODE_OUT="$(sudo stat --format='%a' $MPT/testmode0)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "creat -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' $MPT/testmode0)"
if [[ $uid != $UID_OUT ]]; then
    fail "creat -u err $uid ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $MPT/testmode0)"
if [[ $gid != $GID_OUT ]]; then
    fail "creat -g err $gid ${GID_OUT}"
fi

expect_fail ${CLI} mkdir   -- "mkdir with no args should fail"
#
# Test mkdir with custom mode/uid/gid
#
DIRPATH=$MPT/z/y/x/w
${CLI} mkdir -p -m $FMODE -u $uid -g $gid $DIRPATH
FMODE_OUT="$(sudo stat --format='%a' $DIRPATH)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "creat -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' $DIRPATH)"
if [[ $uid != $UID_OUT ]]; then
    fail "creat -u err $uid ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $DIRPATH)"
if [[ $gid != $GID_OUT ]]; then
    fail "creat -g err $gid ${GID_OUT}"
fi

expect_good ${CLI} logplay -h                  -- "logplay -h should work"
expect_good ${CLI} logplay $MPT                -- "logplay $MPT should succeed"

expect_good ${CLI} chkread -l $MPT/.meta/.log -- "chkread should succeed on log"
expect_good ${CLI} chkread -s $MPT/.meta/.superblock -- "chkread should succeed on superblock"
expect_good ${CLI} chkread -?               -- "chkread -? should succeed"
expect_fail ${CLI} chkread                  -- "chkread with no args should fail"
expect_fail ${CLI} chkread -s "$MPT/badfilexx" -- "chkread should fail w/bad file"

expect_good ${CLI} logplay -rc $MPT         -- "logplay -rc should succeed"
expect_fail ${CLI} logplay -rm $MPT         -- "logplay with -m and -r should fail"
expect_fail ${CLI} logplay             -- "logplay without MPT arg should fail"
#expect_fail ${CLI} logplay --shadow            -- "shadow logplay with no daxdev should fail"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
	 http://localhost/icache_stats
fi

# Unmount and remount
expect_good sudo $UMOUNT $MPT -- "umount should succeed"
expect_fail findmnt -t famfs $MPT -- "famfs is still mounted at $MPT after umount attempt"

if [[ "$FMODE" == "v1" ]]; then
    expect_good sudo mount$RAW_MOUNT_OPTS $DEV $MPT   -- "mount"

    verify_mounted  "$DEV" "$MPT" "test0 mount"

    echo "this logplay should fail because we haven't done mkmeta yet"
    expect_fail ${CLI} logplay -vvv $MPT   -- "logplay 1 before mkmeta"
    expect_fail ${CLI} logplay             -- "logplay should fail with no args"
    
    # Post mount, re-create the meta files
    expect_good ${CLI} mkmeta $DEV                -- "mkmeta 2"
else
    ${MOUNT} --bouncedax $DEV $MPT  # hit the --bouncedax option
    verify_mounted $DEV $MPT "test0 fuse mount"
fi

expect_good sudo test -f $MPT/.meta/.superblock -- "no superblock file after mkmeta"
expect_good sudo test -f $MPT/.meta/.log        -- "no log file after mkmeta 2"

sudo ls -lR $MPT
expect_good ${CLI} logplay -vvv $MPT        -- "logplay after mkmeta should work"
expect_fail ${CLI} mkmeta                   -- "mkmeta with no args should fail"
expect_good ${CLI} mkmeta -h                -- "mkmeta -h should succeed"
expect_fail ${CLI} mkmeta /tmp/nonexistent  -- "mkmeta on non-existing device should fail"
expect_good ${CLI} mkmeta $DEV              -- "mkmeta repeat should fail"

# Replay the log, recovering the files that existed befure the umount
expect_good ${CLI} logplay -m $MPT   -- "logplay 3 (mmap) should work but be nop"
expect_good ${CLI} logplay -vr $MPT  -- "logplay 4 (read) should work but be nop"

# Re-verify the files from prior to the umount
expect_good ${CLI} verify -S 1 -f $MPT/test1 -- "verify test1 after replay"
expect_good ${CLI} verify -S 2 -f $MPT/test2 -- "verify test2 after replay"
expect_good ${CLI} verify -S 3 -f $MPT/test3 -- "verify test3 after replay"

# Re-check the creat custom mode/uid/gid after remount
# (this tests that the log was populated correctly)
FMODE_OUT="$(sudo stat --format='%a' $MPT/testmode0)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "creat -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' $MPT/testmode0)"
if [[ $uid != $UID_OUT ]]; then
    fail "creat -u err $uid ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $MPT/testmode0)"
if [[ $gid != $GID_OUT ]]; then
    fail "creat -g err $gid ${GID_OUT}"
fi

#
# re-check mkdir -mug
#
echo "re-checking mkdir -mug after remount"
FMODE_OUT="$(sudo stat --format='%a' $DIRPATH)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "mkdir -m err $FMODE ${FMODE_OUT}"
fi
uid_OUT="$(sudo stat --format='%u' $DIRPATH)"
if [[ $uid != $UID_OUT ]]; then
    fail "mkdir -u err $uid ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $DIRPATH)"
if [[ $gid != $GID_OUT ]]; then
    fail "mkdir -g err $gid ${GID_OUT}"
fi

#
# test famfs_check
#

# 'famfs check' doesn't support fuse mode (yet)
if [[ ${FAMFS_MODE} == "v1" ]]; then
    expect_fail ${CLI} check          -- "famfs check with no args should fail"
    expect_good ${CLI} check -?       -- "famfs check -? should succeed"
    expect_fail ${CLI_NOSUDO} check $MPT -- "famfs check without sudo should fail"
    expect_good ${CLI} check $MPT     -- "famfs check should succeed"
    expect_fail ${CLI} check "relpath" -- "famfs check on relpath should fail"
    expect_fail ${CLI} check "/badpath" -- "famfs check on bad path should fail"
    sudo touch $MPT/unmapped_file
    expect_fail ${CLI} check -vvv $MPT -- "famfs check should fail due to unmapped file"
    sudo rm $MPT/unmapped_file
    expect_good ${CLI} check -v $MPT   -- "famfs check should succeed after removing unmapped file"

    expect_good ${CLI_NOSUDO} fsck -hv $MPT -- "fsck without sudo should succeed"
fi




expect_good ${CLI} flush -?          -- "flush -? should work"
expect_fail ${CLI} flush             -- "flush with no args should fail"
expect_fail ${CLI} flush /bogus/file -- "flush of a bogus file should fail"
expect_good ${CLI} flush $(sudo find $MPT -type f -print) -- "flush all files should work"
expect_fail ${CLI} flush -vv $(sudo find $MPT -print)     -- "this flush should report errors"

expect_fail ${FSCK}      -- "fsck with no args should fail"
expect_good ${FSCK} -?   -- "fsck -h should succeed"x
expect_good ${FSCK} $MPT -- "fsck should succeed" "-vv"
expect_good ${FSCK} --human $MPT -- "fsck --human should succeed"

SR="~/smoke.shadow"
mkdir -p $SR/root
expect_good ${CLI} logplay --shadow $SR $MPT -- "shadow logplay should work to $SR"

set +x
echo ":==*************************************************************************"
echo ":==test0 completed successfully"
echo ":==*************************************************************************"
exit 0
