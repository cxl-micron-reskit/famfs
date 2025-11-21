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
sudo $UMOUNT $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"

${FSCK} $MPT && fail "fsck by path should fail when not mounted"
${FSCK} $DEV || fail "fsck by dev should succeed when not mounted"

${MOUNT} $DEV $MPT  || fail "mount should succeed test1 1"

${FSCK} $MPT || fail "fsck by path should succeed when mounted"

# Try exclusive open in driver to make this test good;
# (currently succeeds but it should fail)
${FSCK} $DEV && fail "fsck by dev should fail when mounted"
${FSCK} --force $DEV || fail "fsck by dev when mounted should succeed if forced"

${FSCK} /boguspath          && fail "fsck should fail on bogus path"
${CLI_NOSUDO} fsck bogusrelpath && fail "fsck should fail on bogus relative path"

verify_mounted $DEV $MPT "test1.sh"

#
# Do stuff with files bigger than a page, cautiously
#

F_SMALL=test10
${CLI} creat -r -s 8192 -S 10 $MPT/$F_SMALL   || fail "creat $F_SMALL"
${CLI} verify -S 10 -f $MPT/$F_SMALL || fail "verify $F_SMALL after replay"

F_8M=bigtest0
${CLI} creat -v -r -S 42 -s 0x800000 $MPT/${F_8M}   || fail "creat ${F_8M}"
${CLI} verify -S 42 -f $MPT/${F_8M}                 || fail "${F_8M} mismatch"

${CLI} cp -h                        || fail "cp -h should succeed"

${CLI} cp -vvv $MPT/${F_8M} $MPT/${F_8M}_cp      || fail "cp ${F_8M}"
${CLI} verify -S 42 -f $MPT/${F_8M}_cp || fail "verify ${F_8M}_cp"

${CLI} cp --gid=-1 && fail "cp should fail with negative gid"
${CLI} cp --uid=-1 && fail "cp should fail with negative uid"

#
# mkdir with absolute path
#
${CLI} mkdir -h           || fail "mkdir -h should succeed"
${CLI} mkdir $MPT/subdir || fail "failed to create subdir"
${CLI} mkdir $MPT/subdir && fail "creating existing subdir should fail"
${CLI} mkdir $MPT/${F_8M} && fail "mkdir that collides with existing file should fail"

#
# mkdir to relpath
#

# hmm. non-root can't access fuse mount currently. Test relpath by cd to just above
# the mount point. XXX revisit this ...
PFX=$(dirname "$MPT/..")
cd $MPT/..
${CLI} mkdir $PFX/foo || fail "mkdir relpath"
${CLI} mkdir $PFX/./foo/foo || fail "mkdir relpath 2"
${CLI} mkdir $PFX/foo/foo/./bar || fail "mkdir relpath 3"
${CLI} mkdir $PFX/./foo/foo//bar/baz || fail "mkdir relpath 4"
${CLI} mkdir $PFX/./foo/./foo//bar/baz && fail "mkdir relpath exists should fail"
cd -

#
# mkdir -p
#
${CLI} mkdir ${MPT}                && fail "mkdir <mount point> should fail"
${CLI} mkdir -p ${MPT}                || fail "mkdir -p <mount point> should succeed"
${CLI} mkdir -p ${MPT}/A/B/C          || fail "mkdir -p 1"
${CLI} mkdir -pv ${MPT}/AAAA/BBBB/CCC  || fail "mkdir -p 2"
${CLI} mkdir -pv ${MPT}/A/B/C/w/x/y/z  || fail "mkdir -p 3"

sudo chmod 0666 ${MPT}/A
cd ${MPT}/..
pwd
${CLI} mkdir -p $PFX/A/x/y/z               || fail "mkdir -p 4"
${CLI} mkdir -p $PFX/./A/x/y/z               || fail "mkdir -p 5"
cd -

${CLI} mkdir -pv $MPT/${F_8M}/foo/bar/baz/bing && fail "mkdir -p with a file in path should fail"

if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock  -X POST -d  7 http://localhost/log_level
fi

${CLI} mkdir -pvvv $MPT/a/y/../../../.. && fail "mkdir -p ../../../.. ascended out of famfs"

if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # Dump the icache to the log
    sudo curl  --unix-socket $(./scripts/famfs_shadow.sh /mnt/famfs)/sock  http://localhost/icache_dump
fi

# Cause some eviction and re-reading
echo 2 | sudo tee /proc/sys/vm/drop_caches

${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp0      || fail "cp0 ${F_8M}"
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp1      || fail "cp1 ${F_8M}"
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp2      || fail "cp2 ${F_8M}"
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp3      || fail "cp3 ${F_8M}"
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp4      || fail "cp4 ${F_8M}"
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp5      || fail "cp5 ${F_8M}"
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp6      || fail "cp6 ${F_8M}"
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp7      || fail "cp7 ${F_8M}"
${CLI} cp -v $MPT/${F_8M} $MPT/subdir/${F_8M}_cp8      || fail "cp8 ${F_8M}"
${CLI} cp -v $MPT/${F_8M} $MPT/subdir/${F_8M}_cp9      || fail "cp9 ${F_8M}"

# cp should succeed to existing file of the correct size
${CLI} cp $MPT/${F_8M} $MPT/subdir/${F_8M}_cp0     || fail "overcopy should work"
${CLI} cp $MPT/test10 $MPT/subdir/${F_8M}_cp0 && fail "overcopy wrong size should fail"


#
# Copy stuff that is invalid
#
${CLI} cp /tmp/nonexistent_file $MPT/ && fail "cp nonexistent should fail"
${CLI} cp /dev/zero $MPT/             && fail "cp /dev/zero (cardev) should fail"


# cp to an existing dir target
#
${CLI} mkdir $MPT/dirtarg || fail "failed to create subdir dirtarg"
# cp file to directory (name should be basename of source file)
${CLI} cp $MPT/${F_8M} $MPT/dirtarg      || fail "cp to dir ${F_8M}"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M} || fail "verify dirtarg/${F_8M}"

${CLI} logplay -n $MPT

# Cause some eviction and re-reading
echo 2 | sudo tee /proc/sys/vm/drop_caches

${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp0 || fail "verify ${F_8M}_cp0"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp1 || fail "verify ${F_8M}_cp1"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp2 || fail "verify ${F_8M}_cp2"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp3 || fail "verify ${F_8M}_cp3"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp4 || fail "verify ${F_8M}_cp4"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp5 || fail "verify ${F_8M}_cp5"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp6 || fail "verify ${F_8M}_cp6"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp7 || fail "verify ${F_8M}_cp7"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp8 || fail "verify ${F_8M}_cp8"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp9 || fail "verify ${F_8M}_cp9"

#
# Cp wildcard to directory from mkdir -p, and verify
# (with custom mode/uid/gid)
FMODE="600"
UID=$(id -u)
GID=$(id -g)

cd ${MPT}/..
DEST=A/B/C/w/x/y/z
${CLI} cp -m $FMODE -u $UID -g $GID  $MPT/subdir/* $MPT/${DEST} || fail "cp wildcard set to directory should succeed"
# Verify files from wildcard cp, in a deep directory
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp0 || fail "verify relpath ${F_8M}_cp0"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp1 || fail "verify relpath ${F_8M}_cp1"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp2 || fail "verify relpath ${F_8M}_cp2"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp3 || fail "verify relpath ${F_8M}_cp3"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp4 || fail "verify relpath ${F_8M}_cp4"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp5 || fail "verify relpath ${F_8M}_cp5"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp6 || fail "verify relpath ${F_8M}_cp6"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp7 || fail "verify relpath ${F_8M}_cp7"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp8 || fail "verify relpath ${F_8M}_cp8"
${CLI} verify -S 42 -f $PFX/${DEST}/${F_8M}_cp9 || fail "verify relpath ${F_8M}_cp9"
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

sudo $UMOUNT $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
${MOUNT} $DEV $MPT || fail "mount should succeed test1 2"
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

${CLI} verify -S 42 -f $MPT/${F_8M}_cp || fail "verify ${F_8M}_cp"

${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp0 || fail "verify ${F_8M}_cp0"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp1 || fail "verify ${F_8M}_cp1"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp2 || fail "verify ${F_8M}_cp2"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp3 || fail "verify ${F_8M}_cp3"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp4 || fail "verify ${F_8M}_cp4"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp5 || fail "verify ${F_8M}_cp5"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp6 || fail "verify ${F_8M}_cp6"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp7 || fail "verify ${F_8M}_cp7"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp8 || fail "verify ${F_8M}_cp8"
${CLI} verify -S 42 -f $MPT/subdir/${F_8M}_cp9 || fail "verify ${F_8M}_cp9"

#
# Cp wildcard to directory
#
${CLI} cp $MPT/subdir/* $MPT/dirtarg || fail "cp wildcard set to directory should succeed"
# Verify files from wildcard cp
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp0 || fail "verify wildcard ${F_8M}_cp0"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp1 || fail "verify wildcard ${F_8M}_cp1"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp2 || fail "verify wildcard ${F_8M}_cp2"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp3 || fail "verify wildcard ${F_8M}_cp3"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp4 || fail "verify wildcard ${F_8M}_cp4"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp5 || fail "verify wildcard ${F_8M}_cp5"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp6 || fail "verify wildcard ${F_8M}_cp6"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp7 || fail "verify wildcard ${F_8M}_cp7"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp8 || fail "verify wildcard ${F_8M}_cp8"
${CLI} verify -S 42 -f $MPT/dirtarg/${F_8M}_cp9 || fail "verify wildcard ${F_8M}_cp9"


#
# Verify files after remount, from wildcard cp, in a deep directory
#
cd $MPT
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp0 || fail "verify relpath ${F_8M}_cp0"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp1 || fail "verify relpath ${F_8M}_cp1"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp2 || fail "verify relpath ${F_8M}_cp2"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp3 || fail "verify relpath ${F_8M}_cp3"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp4 || fail "verify relpath ${F_8M}_cp4"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp5 || fail "verify relpath ${F_8M}_cp5"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp6 || fail "verify relpath ${F_8M}_cp6"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp7 || fail "verify relpath ${F_8M}_cp7"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp8 || fail "verify relpath ${F_8M}_cp8"
${CLI} verify -S 42 -f ${DEST}/${F_8M}_cp9 || fail "verify relpath ${F_8M}_cp9"
cd -

${CLI} mkdir $MPT/dirtarg2 || fail "failed to create dirtarg2"
# This directory wil cause cp * from dirtarg/ to return non-zero since it
# can't copy the directory
${CLI} mkdir $MPT/dirtarg/foo || fail "failed to create dir foo"

#
# This wildcard copy is also via relative paths
#
cd $MPT/..
${CLI} cp $PFX/dirtarg/* $PFX/dirtarg2 \
     && fail "cp wildcard should succeed but return nonzero when there is a directory that matches the wildcard"
cd -

# The files should have been copied in the command above; it's just that there
# is a directory dirtarg/foo which will not have been copied because we used
# a wildcard, but not --recursive

# Should still be able to verify the files in dirtarg2
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp0 || fail "verify wildcard 2 ${F_8M}_cp0"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp1 || fail "verify wildcard 2 ${F_8M}_cp1"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp2 || fail "verify wildcard 2 ${F_8M}_cp2"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp3 || fail "verify wildcard 2 ${F_8M}_cp3"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp4 || fail "verify wildcard 2 ${F_8M}_cp4"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp5 || fail "verify wildcard 2 ${F_8M}_cp5"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp6 || fail "verify wildcard 2 ${F_8M}_cp6"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp7 || fail "verify wildcard 2 ${F_8M}_cp7"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp8 || fail "verify wildcard 2 ${F_8M}_cp8"
${CLI} verify -S 42 -f $MPT/dirtarg2/${F_8M}_cp9 || fail "verify wildcard 2 ${F_8M}_cp9"

${CLI} mkdir $MPT/smalldir || fail "failed to create smalldir"
${CLI} cp $MPT/dirtarg/${F_8M}_cp0 $MPT/smalldir || fail "cp to smalldir 0"
${CLI} cp $MPT/dirtarg/${F_8M}_cp1 $MPT/smalldir || fail "cp to smalldir 0"
${CLI} mkdir $MPT/smalldir2 || fail "failed to create smalldir2"

${CLI} cp -r $MPT/smalldir $MPT/${F_8M} && fail "recursive copy to file destination should fail"
${CLI} cp $MPT/smalldir/* $MPT/${F_8M}  && fail "wildcard multi-file copy to file destination should fail"

#
# cp -r with absolute paths
#
${CLI} cp -rv $MPT/smalldir/* $MPT/smalldir2 || fail "recursive copy 0"
${CLI} cp -c -rv $MPT/smalldir/* $MPT/smalldir2 || fail "recursive compare 0"
sudo diff -r $MPT/smalldir $MPT/smalldir2 || fail "diff -r smalldir smalldir2"

# deface one of the files and re-compare: should be error
overwrite_page $MPT/smalldir2/bigtest0_cp1 0
sudo diff -r $MPT/smalldir $MPT/smalldir2 && fail "cp -cr smalldir smalldir2"
${CLI} cp -c -r $MPT/smalldir/* $MPT/smalldir2 && fail "recursive compare 1"

${CLI} cp -r $MPT/A $MPT/A-prime || fail "cp -r A A-prime"
sudo diff -r $MPT/A $MPT/A-prime || fail "diff -r A A-prime"
#
# cp -r with relative paths
#
cd $MPT/..
${CLI} cp -r $PFX/A $PFX/A-double-prime           || fail "cp -r A A-double-prime"
sudo diff -r $PFX/A $PFX/A-double-prime
cd -

# Bad cp -r
${CLI} cp -r $MPT/A $MPT/bar/foo     && fail "cp -r to bogus path should fail"
${CLI} cp -r $MPT/A $MPT/${F_8M}        && fail "cp -r to file"
${CLI} cp -r $MPT/A $MPT/${F_8M}/foo    && fail "cp -r to path that uses file as dir"


${CLI} cp $MPT/A/B/C/w/x/y/z/* $MPT || fail "cp valid wildcard to mount pt dir should succeed"

sudo touch /tmp/emptyfile
${CLI} cp /tmp/emptyfile $MPT/emptyfile2 && fail "cp with empty source file should fail"

${FSCK} $MPT || fail "fsck should succeed"
${FSCK} -m $MPT || fail "fsck -mh should succeed"
${FSCK} -vv $MPT || fail "fsck -vv should succeed"
${FSCK} -r $MPT  || fail "fsck -r $MPT should succeed"
${FSCK} -rm $MPT && fail "fsck -r -m $MPT should fail"

sudo cmp $MPT/bigtest0 $MPT/bigtest0_cp       || fail "copies should match"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test1.shadow $MPT

set +x
echo ":==*************************************************************************"
echo ":==test1 completed successfully"
echo ":==*************************************************************************"
exit 0
