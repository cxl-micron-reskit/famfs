#!/usr/bin/env bash

CWD=$(pwd)
# Defaults
DEV="/dev/pmem0"
SCRIPTS=$CWD/scripts
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
BIN=$CWD/release

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
	(-m|--mpt)
	    MPT=$1
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
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

MKFS="sudo $BIN/mkfs.famfs"
CLI="sudo $BIN/famfs"
CLI_NOSUDO="$BIN/famfs"
TEST="stress_prepare"

source $SCRIPTS/test_funcs.sh

sudo mkdir -p $MPT || fail "mkdir"

# Make sure famfs is not mounted
findmnt -t famfs $MPT
if (( $? == 0 )); then
    sudo umount $MPT
fi

# Make sure famfs is not currently mounted at $MPT
verify_not_mounted $DEV $MPT "stress_prepare.sh"

# We now expect the module to already be loaded, but no harm in modprobe to make double sure
sudo modprobe famfs       || fail "modprobe"

# destroy famfs file system, if any
${MKFS} -f -k $DEV     || fail "mkfs/kill"
${MKFS}  $DEV          || fail "mkfs"
${CLI} fsck $DEV       || fail "fsck"
${CLI} mount $DEV $MPT || fail "mount failed"

grep $MPT /proc/mounts              || fail "Mount pt $MPT not in /proc/mounts~"

${CLI} mkmeta $DEV    || fail "mkmeta"
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "prep: no log file after mkmeta"

${CLI} fsck --human $MPT || fail "prep: fsck --human should succeed"

echo "*************************************************************************"
echo "stress_prepare.sh completed successfully"
echo "*************************************************************************"
exit 0
