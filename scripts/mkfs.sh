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
    SCRIPTS=./scripts
else
    BIN=../debug
    SCRIPTS=../scripts
fi
echo "BIN $BIN"
echo "SCRIPTS $SCRIPTS"

# Defaults
DEV="/dev/dax0.0"
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
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Make sure famfs is not currently mounted
grep -c famfs /proc/mounts         && fail "famfs is currently mounted"

# destroy famfs file system, if any
${MKFS} -f -k $DEV    || fail "mkfs/kill"
${MKFS}  $DEV         || fail "mkfs"

set +x
echo "*************************************************************************************"
echo " famfs mkfs successful on device $DEV"
echo "*************************************************************************************"

