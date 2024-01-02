#!/usr/bin/env bash

cwd=$(pwd)

#
# This is a  bit clumsy, but it allows this script to be run either from within the
# user/scripts directory, or from one level above (as I normally want to do)
#
script_name="$0"
echo "script_name: $script_name"
script_relpath=$(dirname $script_name)
script_relpath=$(basename $script_relpath) # get rid of ./, if any
echo "script_repath: $script_relpath"
if [[ "$script_relpath" == "scripts" ]]; then
    BIN=./debug
    KMOD=../kmod
    SCRIPTS=./scripts
else
    BIN=../debug
    KMOD=../../kmod
    SCRIPTS=../scripts
fi
echo "BIN $BIN"
echo "KMOD $KMOD"
echo "SCRIPTS $SCRIPTS"

# Defaults
DEV="/dev/pmem0"
VG=""
MPT=/mnt/famfs
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"

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
	    MPT=$1  # Mount point
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
	    VG=${VALGRIND_ARG}
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
CLI_NOSUDO="$VG $BIN/famfs"


#
# Mounting famfs consists of the kernel mount (the mount command), followed by:
# famfs mkmeta
# famfs logplay
#

set -x

sudo mkdir -p $MPT || fail "mkdir"

${CLI} fsck $DEV          || fail "fsck"

# The famfs kernel module is needed, but might already be loaded
sudo insmod $KMOD/famfs.ko

sudo mount $MOUNT_OPTS $DEV $MPT || fail "mount"

grep famfs /proc/mounts             || fail "No famfs mounted"
grep $DEV /proc/mounts              || fail "dev=$DEV not in /proc/mounts~"
grep $MPT /proc/mounts              || fail "Mount pt $MPT not in /proc/mounts~"

${CLI} mkmeta $DEV        || fail "mkmeta"
sudo test -f $MPT/.meta/.superblock || fail "no superblock file after mkmeta"
sudo test -f $MPT/.meta/.log        || fail "no log file after mkmeta"

${CLI} logplay $MPT || fail "logplay"

set +x
echo "*************************************************************************************"
echo " famfs mount successful: device $DEV mounted at $MPT"
echo "*************************************************************************************"
