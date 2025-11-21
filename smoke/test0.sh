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
    else
        MOUNT_OPTS="--nofuse" # Can drop this b/c fuse is the default
	MKFS_OPTS=""
    fi
else
    echo "FAMFS_MODE: invalid"
    exit 1;
fi

MOUNT="sudo $VG $BIN/famfs mount $MOUNT_OPTS"
MKFS="sudo $VG $BIN/mkfs.famfs $MKFS_OPTS"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
TEST="test0"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Start with a clean, empty file systeem
famfs_recreate  "test0"

grep famfs /proc/mounts             || fail "No famfs mounted"
grep $DEV /proc/mounts              || fail "dev=$DEV not in /proc/mounts~"
grep $MPT /proc/mounts              || fail "Mount pt $MPT not in /proc/mounts~"

#${CLI} mkmeta $DEV        || fail "mkmeta" # refactored - already fully mounted
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "no log file after mkmeta"

# Basic CLI stuff
${CLI} && fail "cli with no subcommand should fail"
${CLI} -h || fail "famfs -h should work"
${CLI} -h mount || fail "famfs -h mount should work"

# Create 1 file and verify
${CLI} creat -h                           || fail "creat -h should succeed"
${CLI} creat                              && fail "creat with no args should fail"
${CLI} creat -r -S 1 $MPT/test1           && fail "creat without size should fail"
${CLI} creat -S -s 10 $MPT/badf           && fail "creat with -S but no -r should fail"
${CLI} creat -r -s 4096 -S 1 $MPT/test1   || fail "creat test1"
${CLI} creat $MPT/.meta            && fail "creat an existing directory should fail"
${CLI} creat -S 1 -r -m "$MPT/zork,4K,1" && fail "multi and single mode should fail"
${CLI} creat -t 1000 -m "$MPT/zork,4K,1" && fail "threadct=1000 should fail"
${CLI} creat -v -s 4K "/tmp/notfamfs"
${CLI} creat -v --multi "$MPT/goodfile,4k" \
       --multi "/tmp/badfile,4M"         && fail "one good, one bad file should be err"
sudo test -f "$MPT/goodfile" || fail "...but the good file should be created"
${CLI} creat -S 1 -r -s 2048 "$MPT/goodfile"\
                                       && fail "randomize with wrong size should fail"
${CLI} creat -S 1 -r "$MPT/goodfile"  || fail "randomize without size should work"
${CLI} creat -t 1000 --multi "$MPT/goodfile,2K,1" \
                                  && fail "randomize with bad threadct should fail"
${CLI} creat -t 0 --multi "$MPT/goodfile,2K,1" \
                                  && fail "randomize with bad threadct should fail 2"
${CLI} creat -t -1 "$MPT/goodfile,2K,1" \
                                  && fail "randomize with bad threadct should fail 3"
${CLI} creat -S 1 -r -M "$MPT/notcreated,22" && fail "seed and multi incompatible"
${CLI} creat -M "$MPT/notcreated"     && fail "multi with no size should fail"
${CLI} creat -M "$MPT/notcreated"     && fail "multi with no size should fail"
${CLI} creat -M "$MPT/notcr,22,22,22" && fail "multi with too many params should fail"
${CLI} creat -M "$MPT/seeded,2M,42" \
       -M "$MPT/notseeded,2M" || fail "multi-create partially seeded should work"
${CLI} verify --multi "$MPT/seeded,42" || fail "seeded file should verify"

${CLI} verify -h                 || fail "verify -h should succeed"
${CLI} verify                    && fail "verify with no args should fail"
${CLI} verify -f $MPT/test1      && fail "verify with no seed should fail"
${CLI} verify -S 1 -f badfile    && fail "verify with bad filename should fail"
${CLI} verify -S 1 -f $MPT/test1 || fail "verify 1 after creat"
${CLI} verify -S 99 -f $MPT/test1 && fail "verify with wrong seed shoud fail"


# Create 2 more files
${CLI} creat -r -s 4096 -S 2 $MPT/test2   || fail "creat test2"
${CLI} creat -r -s 4096 -S 3 $MPT/test3   || fail "creat test3"

# Verify all 3 files
${CLI} verify -S 1 -f $MPT/test1 || fail "verify 1 after multi creat"
${CLI} verify -S 2 -f $MPT/test2 || fail "verify 2 after multi creat"
${CLI} verify --quiet -S 3 -f $MPT/test3 || fail "verify 3 after multi creat"

${CLI} verify -S 99 -f $MPT/test1 && fail "verify with bad seed should fail"
${CLI} verify -m "$MPT/test1,1" || fail "verify --multi should succeed"
${CLI} verify -m "$MPT/test1,2" && fail "verify --multi w/bad seed should fail"

${CLI} verify --multi "$MPT/test1" && fail "no comma in verify multi arg"
${CLI} verify --multi "$MPT/test1,1,extra" && fail "extra fields in multi"
${CLI} verify --multi "$MPT/test1,1" \
       --multi "$MPT/test2,2" \
       --multi "$MPT/test3,3" \
    || fail "verify multi with good seeds should succeed"
${CLI} verify --multi "$MPT/test1,9" \
       --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    && fail "verify multi with any bad seeds should fail"
#bad thread counts
${CLI} verify --threadct -1 \
       --multi "$MPT/test1,1" --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    && fail "verify multi with good seeds and -1 thread should fail"
${CLI} verify --threadct 257 \
       --multi "$MPT/test1,1" --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    && fail "verify multi with good seeds and 257 thread should fail"
${CLI} verify -t 999 \
       --multi "$MPT/test1,9" --multi "$MPT/test2,2" --multi "$MPT/test3,3" \
    && fail "verify multi with any bad seeds and bad threadct fail"

# Create same file should fail unless we're randomizing it
${CLI} creat -r -s 4096 -S 99 $MPT/test1 || fail "Create to re-init existing file should succeed"
${CLI} creat -s 4096 $MPT/test1  || fail "Recreate with same size should succeed"
${CLI} creat -s 8192 $MPT/test1  && fail "Recreate with different size should fail"

# Put back data for seed=1 because we're gonna check it later
${CLI} creat -r -s 4096 -S 1 $MPT/test1  || fail "Create to re-init existing file should succeed"

# Create outside famfs should fail
${CLI} creat -r -s 4096 -S 1 /tmp/test1   && fail "Create should fail if file exists"

# Create an empty file should fail
${CLI} creat -r -s 0 -S 1 $MPT/emptyfile  && fail "Create empty file should fail"

# Test creat mode/uid/gid options
# These permissions should make it work without sudo
FMODE="600"
UID=$(id -u)
GID=$(id -g)
${CLI} creat -s 0x100000 -r -m $FMODE -u $UID -g $GID $MPT/testmode0 || fail "creat with mode/uid/gid"

#
# Check creat with the custom mode/uid/gid
#
FMODE_OUT="$(sudo stat --format='%a' $MPT/testmode0)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "creat -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' $MPT/testmode0)"
if [[ $UID != $UID_OUT ]]; then
    fail "creat -u err $UID ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $MPT/testmode0)"
if [[ $GID != $GID_OUT ]]; then
    fail "creat -g err $GID ${GID_OUT}"
fi

${CLI} mkdir   && fail "mkdir with no args should fail"
#
# Test mkdir with custom mode/uid/gid
#
DIRPATH=$MPT/z/y/x/w
${CLI} mkdir -p -m $FMODE -u $UID -g $GID $DIRPATH
FMODE_OUT="$(sudo stat --format='%a' $DIRPATH)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "creat -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' $DIRPATH)"
if [[ $UID != $UID_OUT ]]; then
    fail "creat -u err $UID ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $DIRPATH)"
if [[ $GID != $GID_OUT ]]; then
    fail "creat -g err $GID ${GID_OUT}"
fi

${CLI} logplay -h                  || fail "logplay -h should work"
${CLI} logplay $MPT                || fail "logplay $MPT should succeed"

${CLI} chkread -l $MPT/.meta/.log        || fail "chkread should succeed on log"
${CLI} chkread -s $MPT/.meta/.superblock || fail "chkread should succeed on superblock"
${CLI} chkread -?                        || fail "chkread -? should succeed"
${CLI} chkread                           && fail "chkread with no args should fail"
${CLI} chkread -s "$MPT/badfilexx" && fail "chkread should fail w/bad file"

${CLI} logplay -rc $MPT            || fail "logplay -rc should succeed"
${CLI} logplay -rm $MPT            && fail "logplay with -m and -r should fail"
${CLI} logplay                     && fail "logplay without MPT arg should fail"
#${CLI} logplay --shadow            && fail "shadow logplay with no daxdev should fail"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
	 http://localhost/icache_stats
fi

# Unmount and remount
sudo $UMOUNT $MPT || fail "umount should succeed"
findmnt -t famfs $MPT && fail "famfs is still mounted at $MPT after umount attempt"

if [[ "$FMODE" == "v1" ]]; then
    sudo mount$RAW_MOUNT_OPTS $DEV $MPT   || fail "mount"

    #grep -c famfs /proc/mounts         || fail "famfs not mounted after remount attempt"
    verify_mounted  "$DEV" "$MPT" "test0 mount"

    echo "this logplay should fail because we haven't done mkmeta yet"
    ${CLI} logplay -vvv $MPT               && fail "logplay 1 before mkmeta"
    ${CLI} logplay                       && fail "logplay should fail with no args"
    
    # Post mount, re-create the meta files
    ${CLI} mkmeta $DEV                || fail "mkmeta 2"
else
    ${MOUNT} --bouncedax $DEV $MPT  # hit the --bouncedax option
    verify_mounted $DEV $MPT "test0 fuse mount"
fi

sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "no log file after mkmeta 2"

sudo ls -lR $MPT
${CLI} logplay -vvv $MPT             || fail "logplay after mkmeta should work"
${CLI} mkmeta                    && fail "mkmeta with no args should fail"
${CLI} mkmeta -h                 || fail "mkmeta -h should succeed"
${CLI} mkmeta /tmp/nonexistent   && fail "mkmeta on non-existing device should fail"
${CLI} mkmeta $DEV               || fail "mkmeta repeat should fail"

# Replay the log, recovering the files that existed befure the umount
${CLI} logplay -m $MPT           || fail "logplay 3 (mmap) should work but be nop"
${CLI} logplay -vr $MPT          || fail "logplay 4 (read) should work but be nop"

# Re-verify the files from prior to the umount
${CLI} verify -S 1 -f $MPT/test1 || fail "verify test1 after replay"
${CLI} verify -S 2 -f $MPT/test2 || fail "verify test2 after replay"
${CLI} verify -S 3 -f $MPT/test3 || fail "verify test3 after replay"

# Re-check the creat custom mode/uid/gid after remount
# (this tests that the log was populated correctly)
FMODE_OUT="$(sudo stat --format='%a' $MPT/testmode0)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "creat -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' $MPT/testmode0)"
if [[ $UID != $UID_OUT ]]; then
    fail "creat -u err $UID ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $MPT/testmode0)"
if [[ $GID != $GID_OUT ]]; then
    fail "creat -g err $GID ${GID_OUT}"
fi

#
# re-check mkdir -mug
#
echo "re-checking mkdir -mug after remount"
FMODE_OUT="$(sudo stat --format='%a' $DIRPATH)"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "mkdir -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' $DIRPATH)"
if [[ $UID != $UID_OUT ]]; then
    fail "mkdir -u err $UID ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' $DIRPATH)"
if [[ $GID != $GID_OUT ]]; then
    fail "mkdir -g err $GID ${GID_OUT}"
fi

#
# test famfs_check
#

# 'famfs check' doesn't support fuse mode (yet)
if [[ ${FAMFS_MODE} == "v1" ]]; then
    ${CLI} check                  && fail "famfs check with no args should fail"
    ${CLI} check -?               || fail "famfs check -? should succeed"
    ${CLI_NOSUDO} check $MPT      && fail "famfs check without sudo should fail"
    ${CLI} check $MPT             || fail "famfs check should succeed"
    ${CLI} check "relpath"        && fail "famfs check on relpath should fail"
    ${CLI} check "/badpath"       && fail "famfs check on bad path should fail"
    sudo touch $MPT/unmapped_file
    ${CLI} check -vvv $MPT        && fail "famfs check should fail due to unmapped file"
    sudo rm $MPT/unmapped_file
    ${CLI} check -v $MPT          || fail "famfs check should succeed after removing unmapped file"

    ${CLI_NOSUDO} fsck -hv $MPT || fail "fsck without sudo should succeed"
fi




${CLI} flush -?          || fail "flush -? should work"
${CLI} flush             && fail "flush with no args should fail"
${CLI} flush /bogus/file && "flush of a bogus file should fail"
${CLI} flush $(sudo find $MPT -type f -print) || fail "flush all files should work"
${CLI} flush -vv $(sudo find $MPT -print)     && fail "this flush should report errors"

${CLI} fsck      && fail "fsck with no args should fail"
${CLI} fsck -?   || fail "fsck -h should succeed"x
${CLI} fsck $MPT || fail_fsck "fsck should succeed" "-vv"
${CLI} fsck --human $MPT || fail "fsck --human should succeed"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test0.shadow $MPT

set +x
echo ":==*************************************************************************"
echo ":==test0 completed successfully"
echo ":==*************************************************************************"
exit 0
