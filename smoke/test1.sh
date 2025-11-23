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
TEST="test1"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Start with a clean, empty file systeem
famfs_recreate "test1"

verify_mounted $DEV $MPT "test1.sh"
expect_good sudo $UMOUNT $MPT -- "umount"
verify_not_mounted $DEV $MPT "test1.sh"

expect_fail ${FSCK} $MPT -- "fsck by path should fail when not mounted"
expect_good ${FSCK} $DEV -- "fsck by dev should succeed when not mounted"

expect_good ${MOUNT} $DEV $MPT  -- "mount should succeed test1 1"

expect_good ${FSCK} $MPT -- "fsck by path should succeed when mounted"

# Try exclusive open in driver to make this test good;
# (currently succeeds but it should fail)
expect_fail ${FSCK} $DEV -- "fsck by dev should fail when mounted"
expect_good ${FSCK} --force $DEV -- "fsck by dev when mounted should succeed if forced"

expect_fail ${FSCK} /boguspath          -- "fsck should fail on bogus path"
expect_fail ${CLI_NOSUDO} fsck bogusrelpath -- "fsck should fail on bogus relative path"

verify_mounted $DEV $MPT "test1.sh"

#
# Do stuff with files bigger than a page, cautiously
#

F_SMALL=test10
expect_good ${CLI} creat -r -s 8192 -S 10 $MPT/$F_SMALL   -- "creat $F_SMALL"
expect_good ${CLI} verify -S 10 -f $MPT/$F_SMALL -- "verify $F_SMALL after replay"

F_8M=bigtest0
expect_good ${CLI} creat -v -r -S 42 -s 0x800000 $MPT/${F_8M}   -- "creat ${F_8M}"
expect_good ${CLI} verify -S 42 -f $MPT/${F_8M}                 -- "${F_8M} mismatch"

expect_good ${CLI} cp -h                        -- "cp -h should succeed"

expect_good ${CLI} cp -vvv $MPT/${F_8M} $MPT/${F_8M}_cp      -- "cp ${F_8M}"
expect_good ${CLI} verify -S 42 -f $MPT/${F_8M}_cp -- "verify ${F_8M}_cp"

expect_fail ${CLI} cp --gid=-1 -- "cp should fail with negative gid"
expect_fail ${CLI} cp --uid=-1 -- "cp should fail with negative uid"

#
# mkdir with absolute path
#
expect_good ${CLI} mkdir -h           -- "mkdir -h should succeed"
expect_good ${CLI} mkdir $MPT/subdir -- "failed to create subdir"
expect_fail ${CLI} mkdir $MPT/subdir -- "creating existing subdir should fail"
expect_fail ${CLI} mkdir $MPT/${F_8M} -- "mkdir that collides with existing file should fail"

#
# mkdir to relpath
#

# hmm. non-root can't access fuse mount currently. Test relpath by cd to just above
# the mount point. XXX revisit this ...
PFX=$(dirname "$MPT/..")
cd $MPT/..
expect_good ${CLI} mkdir $PFX/foo -- "mkdir relpath"
expect_good ${CLI} mkdir $PFX/./foo/foo -- "mkdir relpath 2"
expect_good ${CLI} mkdir $PFX/foo/foo/./bar -- "mkdir relpath 3"
expect_good ${CLI} mkdir $PFX/./foo/foo//bar/baz -- "mkdir relpath 4"
expect_fail ${CLI} mkdir $PFX/./foo/./foo//bar/baz -- "mkdir relpath exists should fail"
cd -

#
# mkdir -p
#
expect_fail ${CLI} mkdir ${MPT}                -- "mkdir <mount point> should fail"
expect_good ${CLI} mkdir -p ${MPT}                -- "mkdir -p <mount point> should succeed"
expect_good ${CLI} mkdir -p ${MPT}/A/B/C          -- "mkdir -p 1"
expect_good ${CLI} mkdir -pv ${MPT}/AAAA/BBBB/CCC  -- "mkdir -p 2"
expect_good ${CLI} mkdir -pv ${MPT}/A/B/C/w/x/y/z  -- "mkdir -p 3"

sudo chmod 0666 ${MPT}/A
cd ${MPT}/..
pwd
expect_good ${CLI} mkdir -p $PFX/A/x/y/z               -- "mkdir -p 4"
expect_good ${CLI} mkdir -p $PFX/./A/x/y/z               -- "mkdir -p 5"
cd -

expect_fail ${CLI} mkdir -pv $MPT/${F_8M}/foo/bar/baz/bing -- "mkdir -p with a file in path should fail"

if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock  -X POST -d  7 http://localhost/log_level
fi

expect_fail ${CLI} mkdir -pvvv $MPT/a/y/../../../.. -- "mkdir -p ../../../.. ascended out of famfs"

if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # Dump the icache to the log
    sudo curl  --unix-socket $(./scripts/famfs_shadow.sh /mnt/famfs)/sock  http://localhost/icache_dump
fi

# Cause some eviction and re-reading
echo 2 | sudo tee /proc/sys/vm/drop_caches

expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp0      -- "cp0 ${F_8M}"
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp1      -- "cp1 ${F_8M}"
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp2      -- "cp2 ${F_8M}"
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp3      -- "cp3 ${F_8M}"
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp4      -- "cp4 ${F_8M}"
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp5      -- "cp5 ${F_8M}"
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp6      -- "cp6 ${F_8M}"
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp7      -- "cp7 ${F_8M}"
expect_good ${CLI} cp -v $MPT/${F_8M} $MPT/subdir/${F_8M}_cp8      -- "cp8 ${F_8M}"
expect_good ${CLI} cp -v $MPT/${F_8M} $MPT/subdir/${F_8M}_cp9      -- "cp9 ${F_8M}"

# cp should succeed to existing file of the correct size
expect_good ${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp0     -- "overcopy should work"
expect_fail ${CLI} cp $MPT/test10 $MPT/subdir/${F_8M}_cp0 -- "overcopy wrong size should fail"


#
# Copy stuff that is invalid
#
expect_fail ${CLI} cp /tmp/nonexistent_file $MPT/ -- "cp nonexistent should fail"
expect_fail ${CLI} cp /dev/zero $MPT/             -- "cp /dev/zero (cardev) should fail"


# cp to an existing dir target
#
expect_good ${CLI} mkdir $MPT/dirtarg -- "failed to create subdir dirtarg"
# cp file to directory (name should be basename of source file)
expect_good ${CLI} cp $MPT/${F_8M} $MPT/dirtarg      -- "cp to dir ${F_8M}"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M} -- "verify dirtarg/${F_8M}"

${CLI} logplay -n $MPT

# Cause some eviction and re-reading
echo 2 | sudo tee /proc/sys/vm/drop_caches

expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp0 -- "verify ${F_8M}_cp0"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp1 -- "verify ${F_8M}_cp1"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp2 -- "verify ${F_8M}_cp2"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp3 -- "verify ${F_8M}_cp3"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp4 -- "verify ${F_8M}_cp4"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp5 -- "verify ${F_8M}_cp5"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp6 -- "verify ${F_8M}_cp6"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp7 -- "verify ${F_8M}_cp7"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp8 -- "verify ${F_8M}_cp8"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp9 -- "verify ${F_8M}_cp9"

#
# Cp wildcard to directory from mkdir -p, and verify
# (with custom mode/uid/gid)
FMODE="600"
UID=$(id -u)
GID=$(id -g)

cd ${MPT}/..
DEST=A/B/C/w/x/y/z
expect_good ${CLI} cp -m $FMODE -u $UID -g $GID  $MPT/subdir/* $MPT/${DEST} -- "cp wildcard set to directory should succeed"
# Verify files from wildcard cp, in a deep directory
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp0 -- "verify relpath ${F_8M}_cp0"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp1 -- "verify relpath ${F_8M}_cp1"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp2 -- "verify relpath ${F_8M}_cp2"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp3 -- "verify relpath ${F_8M}_cp3"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp4 -- "verify relpath ${F_8M}_cp4"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp5 -- "verify relpath ${F_8M}_cp5"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp6 -- "verify relpath ${F_8M}_cp6"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp7 -- "verify relpath ${F_8M}_cp7"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp8 -- "verify relpath ${F_8M}_cp8"
expect_good ${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp9 -- "verify relpath ${F_8M}_cp9"
cd -


# Check the custom cp mode/uid/gid on one of the files
FILE="$MPT/${DEST}/${F_8M}_cp0"
FMODE_OUT="$(sudo stat --format='%a' ${FILE})"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "cp -m err $FMODE ${FMODE_OUT}"
fi
UID_OUT="$(sudo stat --format='%u' ${FILE})"
if [[ $UID != $UID_OUT ]]; then
    fail "cp -u err $UID ${UID_OUT}"
fi
GID_OUT="$(sudo stat --format='%g' ${FILE})"
if [[ $GID != $GID_OUT ]]; then
    fail "cp -g err $GID ${GID_OUT}"
fi

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
	 http://localhost/icache_stats
fi

expect_good sudo $UMOUNT $MPT -- "umount"
verify_not_mounted $DEV $MPT "test1.sh"
expect_good ${MOUNT} $DEV $MPT -- "mount should succeed test1 2"
verify_mounted $DEV $MPT "test1.sh"

# re-check the custom cp mode/uid/gid on one of the files
FMODE_OUT="$(sudo stat --format='%a' ${FILE})"
if [[ $FMODE != $FMODE_OUT ]]; then
    fail "cp -m err $MODE ${FMODE_OUT} after remount"
fi
UID_OUT="$(sudo stat --format='%u' ${FILE})"
if [[ $UID != $UID_OUT ]]; then
    fail "cp -u err $UID ${UID_OUT} after remount"
fi
GID_OUT="$(sudo stat --format='%g' ${FILE})"
if [[ $GID != $GID_OUT ]]; then
    fail "cp -g err $GID ${GID_OUT} after remount"
fi

expect_good ${CLI} verify -S 42 -f $MPT/${F_8M}_cp -- "verify ${F_8M}_cp"

expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp0 -- "verify ${F_8M}_cp0"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp1 -- "verify ${F_8M}_cp1"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp2 -- "verify ${F_8M}_cp2"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp3 -- "verify ${F_8M}_cp3"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp4 -- "verify ${F_8M}_cp4"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp5 -- "verify ${F_8M}_cp5"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp6 -- "verify ${F_8M}_cp6"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp7 -- "verify ${F_8M}_cp7"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp8 -- "verify ${F_8M}_cp8"
expect_good ${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp9 -- "verify ${F_8M}_cp9"

#
# Cp wildcard to directory
#
expect_good ${CLI} cp $MPT/subdir/* $MPT/dirtarg -- "cp wildcard set to directory should succeed"
# Verify files from wildcard cp
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp0 -- "verify wildcard ${F_8M}_cp0"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp1 -- "verify wildcard ${F_8M}_cp1"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp2 -- "verify wildcard ${F_8M}_cp2"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp3 -- "verify wildcard ${F_8M}_cp3"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp4 -- "verify wildcard ${F_8M}_cp4"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp5 -- "verify wildcard ${F_8M}_cp5"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp6 -- "verify wildcard ${F_8M}_cp6"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp7 -- "verify wildcard ${F_8M}_cp7"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp8 -- "verify wildcard ${F_8M}_cp8"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp9 -- "verify wildcard ${F_8M}_cp9"


#
# Verify files after remount, from wildcard cp, in a deep directory
#
cd $MPT
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp0 -- "verify relpath ${F_8M}_cp0"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp1 -- "verify relpath ${F_8M}_cp1"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp2 -- "verify relpath ${F_8M}_cp2"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp3 -- "verify relpath ${F_8M}_cp3"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp4 -- "verify relpath ${F_8M}_cp4"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp5 -- "verify relpath ${F_8M}_cp5"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp6 -- "verify relpath ${F_8M}_cp6"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp7 -- "verify relpath ${F_8M}_cp7"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp8 -- "verify relpath ${F_8M}_cp8"
expect_good ${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp9 -- "verify relpath ${F_8M}_cp9"
cd -

expect_good ${CLI} mkdir $MPT/dirtarg2 -- "failed to create dirtarg2"
# This directory wil cause cp * from dirtarg/ to return non-zero since it
# can't copy the directory
expect_good ${CLI} mkdir $MPT/dirtarg/foo -- "failed to create dir foo"

#
# This wildcard copy is also via relative paths
#
cd $MPT/..
expect_fail ${CLI} cp $PFX/dirtarg/* $PFX/dirtarg2 \
     -- "cp wildcard should succeed but return nonzero when there is a directory that matches the wildcard"
cd -

# The files should have been copied in the command above; it's just that there
# is a directory dirtarg/foo which will not have been copied because we used
# a wildcard, but not --recursive

# Should still be able to verify the files in dirtarg2
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp0 -- "verify wildcard 2 ${F_8M}_cp0"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp1 -- "verify wildcard 2 ${F_8M}_cp1"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp2 -- "verify wildcard 2 ${F_8M}_cp2"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp3 -- "verify wildcard 2 ${F_8M}_cp3"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp4 -- "verify wildcard 2 ${F_8M}_cp4"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp5 -- "verify wildcard 2 ${F_8M}_cp5"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp6 -- "verify wildcard 2 ${F_8M}_cp6"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp7 -- "verify wildcard 2 ${F_8M}_cp7"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp8 -- "verify wildcard 2 ${F_8M}_cp8"
expect_good ${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp9 -- "verify wildcard 2 ${F_8M}_cp9"

expect_good ${CLI} mkdir $MPT/smalldir -- "failed to create smalldir"
expect_good ${CLI} cp $MPT/dirtarg/${F_8M}_cp0 $MPT/smalldir -- "cp to smalldir 0"
expect_good ${CLI} cp $MPT/dirtarg/${F_8M}_cp1 $MPT/smalldir -- "cp to smalldir 0"
expect_good ${CLI} mkdir $MPT/smalldir2 -- "failed to create smalldir2"

expect_fail ${CLI} cp -r $MPT/smalldir $MPT/${F_8M} -- "recursive copy to file destination should fail"
expect_fail ${CLI} cp $MPT/smalldir/* $MPT/${F_8M}  -- "wildcard multi-file copy to file destination should fail"

#
# cp -r with absolute paths
#
expect_good ${CLI} cp -rv $MPT/smalldir/* $MPT/smalldir2 -- "recursive copy 0"
expect_good ${CLI} cp -c -rv $MPT/smalldir/* $MPT/smalldir2 -- "recursive compare 0"
expect_good sudo diff -r $MPT/smalldir $MPT/smalldir2 -- "diff -r smalldir smalldir2"

# deface one of the files and re-compare: should be error
overwrite_page $MPT/smalldir2/bigtest0_cp1 0
expect_fail sudo diff -r $MPT/smalldir $MPT/smalldir2 -- "cp -cr smalldir smalldir2"
expect_fail ${CLI} cp -c -r $MPT/smalldir/* $MPT/smalldir2 -- "recursive compare 1"

expect_good ${CLI} cp -r $MPT/A $MPT/A-prime -- "cp -r A A-prime"
expect_good sudo diff -r $MPT/A $MPT/A-prime -- "diff -r A A-prime"
#
# cp -r with relative paths
#
cd $MPT/..
expect_good ${CLI} cp -r $PFX/A $PFX/A-double-prime           -- "cp -r A A-double-prime"
sudo diff -r $PFX/A $PFX/A-double-prime
cd -

# Bad cp -r
expect_fail ${CLI} cp -r $MPT/A $MPT/bar/foo     -- "cp -r to bogus path should fail"
expect_fail ${CLI} cp -r $MPT/A $MPT/${F_8M}        -- "cp -r to file"
expect_fail ${CLI} cp -r $MPT/A $MPT/${F_8M}/foo    -- "cp -r to path that uses file as dir"


expect_good ${CLI} cp $MPT/A/B/C/w/x/y/z/* $MPT -- "cp valid wildcard to mount pt dir should succeed"

sudo touch /tmp/emptyfile
expect_fail ${CLI} cp /tmp/emptyfile $MPT/emptyfile2 -- "cp with empty source file should fail"

expect_good ${FSCK} $MPT -- "fsck should succeed"
expect_good ${FSCK} -m $MPT -- "fsck -mh should succeed"
expect_good ${FSCK} -vv $MPT -- "fsck -vv should succeed"
expect_good ${FSCK} -r $MPT  -- "fsck -r $MPT should succeed"
expect_good ${FSCK} -rm $MPT -- "fsck -r -m $MPT should fail"

expect_good sudo cmp $MPT/bigtest0 $MPT/bigtest0_cp       -- "copies should match"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test1.shadow $MPT

set +x
echo ":==*************************************************************************"
echo ":==test1 completed successfully"
echo ":==*************************************************************************"
exit 0
