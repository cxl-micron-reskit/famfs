#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
VG=""
SCRIPTS=../scripts/
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
RMMOD=0

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
	(-n|--no-rmmod)
	    RMMOD=0
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

echo "DEVTYPE=$DEVTYPE"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
TEST="test_shadow_yaml"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Start with a clean, empty file systeem
famfs_recreate -d "$DEV" -b "$BIN" -m "$MPT" -M "recreate in test0.sh"

verify_mounted $DEV $MPT $TEST

verify_mounted $DEV $MPT "$TEST.sh mounted"
sudo $UMOUNT $MPT || fail "$TEST.sh umount"
verify_not_mounted $DEV $MPT "$TEST.sh"

# Test shadow logplay while the fs is not mounted
SHADOWPATH=/tmp/shadowpath
${CLI} logplay --shadow -d /dev/bogodax && fail "shadow logplay should fail with bogus daxdev"
sudo rm -rf $SHADOWPATH
${CLI} logplay --shadow --daxdev $DEV -vv  $SHADOWPATH/frob && \
    fail "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"
${CLI} logplay --shadow --daxdev $DEV -vv  $SHADOWPATH || \
    fail "shadow logplay to nonexistent shadow dir should succeed if parent exists"
${CLI} logplay --daxdev $DEV -vv  $SHADOWPATH && \
    fail "logplay should fail if --daxdev is set without --shadow"

sudo rm -rf $SHADOWPATH
sudo mkdir -p $SHADOWPATH
${CLI} logplay --shadow --daxdev $DEV -vv  $SHADOWPATH || \
    fail "shadow logplay to existing shadow dir should succeed"
${CLI} logplay --shadow --daxdev $DEV -vv  $SHADOWPATH || \
    fail "redo shadow logplay to existing shadow dir should succeed"

# Double shadow arg means re-parse yaml to test (if the shadow files are not already present)
sudo rm -rf $SHADOWPATH
${CLI} logplay --shadow --shadow --daxdev $DEV  -vv  $SHADOWPATH || \
    fail "shadow logplay with yaml test to existing shadow dir should succeed"

sudo rm -rf /tmp/famfs2
sudo mkdir /tmp/famfs2
${CLI_NOSUDO} logplay --shadow --daxdev $DEV -vv /tmp/famfs2 && \
    fail "shadow logplay to non-writable  shadow dir should succeed"
sudo rm -rf /tmp/famfs2

SHADOWPATH=/tmp/famfs_shadow3
sudo rm -rf $SHADOWPATH
sudo mkdir $SHADOWPATH

sudo cat <<EOF > $SHADOWPATH/0505
---
file:
  path: 0505
  size: 1048576
  flags: 2
  mode: 00
  uid: 0
  gid: 0
  nextents: 1
  simple_ext_list:
  - offset: 0x3fc00000
    length: 0x200000
...
EOF

${CLI} mount $DEV $MPT || fail "remount after shadow yaml test should work"
verify_mounted $DEV $MPT "$TEST.sh mounted 2"

set +x
echo "*************************************************************************"
echo "$TEST completed successfully"
echo "*************************************************************************"
exit 0
