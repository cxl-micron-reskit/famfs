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
	(-n|--no-rmmod)
	    RMMOD=0
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

if [[ "${FAMFS_MODE}" == "v1" || "${FAMFS_MODE}" == "fuse" ]]; then
    echo "FAMFS_MODE: ${FAMFS_MODE}"
    if [[ "${FAMFS_MODE}" == "fuse" ]]; then
        MOUNT_OPTS="-f"
	sudo rmmod famfs
    fi
else
    echo "FAMFS_MODE: invalid"
    exit 1;
fi

MOUNT="sudo $VG $BIN/famfs mount $MOUNT_OPTS"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
CLI_NOSUDO="$VG $BIN/famfs"
TEST="test_shadow_yaml"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

# Start with a clean, empty file systeem
famfs_recreate "test_shadow_yaml"

verify_mounted $DEV $MPT $TEST

verify_mounted $DEV $MPT "$TEST.sh mounted"
sudo $UMOUNT $MPT || fail "$TEST.sh umount"
verify_not_mounted $DEV $MPT "$TEST.sh"

# Test shadow logplay while the fs is not mounted
SHADOWPATH=/tmp/shadowpath
${CLI} logplay --shadow -d /dev/bogodax && fail "shadow logplay should fail with bogus daxdev"
sudo rm -rf $SHADOWPATH
${CLI} logplay --shadow $SHADOWPATH/frob --daxdev $DEV -vv   && \
    fail "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"
${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv  || \
    fail "shadow logplay to nonexistent shadow dir should succeed if parent exists"
${CLI} logplay --daxdev $DEV $SHADOWPATH -vv && \
    fail "logplay should fail if --daxdev is set without --shadow"
${CLI} logplay --shadow /etc/passwd --daxdev $DEV -vv  && \
    fail "shadow logplay to regular file should fail"

sudo rm -rf $SHADOWPATH
sudo mkdir -p $SHADOWPATH
${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv  || \
    fail "shadow logplay to existing shadow dir should succeed"
${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv || \
    fail "redo shadow logplay to existing shadow dir should succeed"

# Double shadow arg means re-parse yaml to test (if the shadow files are not already present)
sudo rm -rf $SHADOWPATH
${CLI} logplay --shadow $SHADOWPATH --shadowtest --daxdev $DEV  -vv  || \
    fail "shadow logplay with yaml test to existing shadow dir should succeed"

sudo rm -rf /tmp/famfs2
sudo mkdir /tmp/famfs2
${CLI_NOSUDO} logplay --shadow /tmp/famfs2 --daxdev $DEV -vv && \
    fail "shadow logplay to non-writable shadow dir should fail"
sudo rm -rf /tmp/famfs2

SHADOWPATH2=/tmp/famfs_shadowpath2
sudo rm -rf $SHADOWPATH2
sudo mkdir $SHADOWPATH2

sudo cat <<EOF > $SHADOWPATH2/0505
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

${MOUNT} $DEV $MPT || fail "remount after shadow yaml test should work"
verify_mounted $DEV $MPT "$TEST.sh mounted 2"

# TODO: move this to new smoke/test_fused.sh
FAMFS_FUSED="sudo $BIN/famfs_fused"

${CLI} creat -s 3g  ${MPT}/memfile       || fail "can't create memfile"
${CLI} creat -s 100m ${MPT}/memfile1     || fail "creat should succeed with -s 100m"
${CLI} creat -s 10000k ${MPT}/memfile2   || fail "creat with -s 10000k should succeed"
${CLI} mkdir ${MPT}/tmpdir || fail "mkdir should succeed"
FUSE_SHADOW="/tmp/s"

FUSE_MPT="/tmp/famfs_fuse"
sudo rm -rf $FUSE_SHADOW
mkdir -p $FUSE_SHADOW $FUSE_MPT

${CLI} logplay --shadow $FUSE_SHADOW --daxdev $DEV  -vv || \
    fail "shadow logplay to ${FUSE_SHADOW} should succeed"

${FAMFS_FUSED} --help || fail "famfs_fused --help should succeed"
${FAMFS_FUSED} --version || fail "famfs_fused --version should succeed"

# Test some bad args
${FAMFS_FUSED} -df -o source=${FUSE_SHADOW} && fail "fused should fail w/missing MPT"
${FAMFS_FUSED} -df  $FUSE_MPT && fail "fused should fail w/missing source"
${FAMFS_FUSED} -o source=/bad/mpt $FUSE_MPT && fail "fused should fail w/bad MPT"
${FAMFS_FUSED} -o source=/etc/passwd $FUSE_MPT && fail "fused should fail w/file as mpt"
${FAMFS_FUSED} -o source=${FUSE_SHADOW} -o foo=bar $FUSE_MPT && \
    fail "fused should fail with bad -o opt (-o foo=bar)"

# Get the kernel version string (e.g., "5.15.0-27-generic")
kernel_version=$(uname -r)

# Use a regex to extract the major and minor version numbers
if [[ $kernel_version =~ ^([0-9]+)\.([0-9]+) ]]; then
    major=${BASH_REMATCH[1]}
    minor=${BASH_REMATCH[2]}
else
    echo "Error: Unable to parse the kernel version: $kernel_version" >&2
    exit 1
fi

echo "Major Version: $major"
echo "Minor Version: $minor"

if [[ "${FAMFS_MODE}" == "fuse" ]]; then
    # Stuff that should fail
    sudo truncate --size 0 $MPT/memfile     && fail "truncate fuse file should fail"
    sudo mkdir $MPT/mydir                   && fail "mkdir should fail in fuse"
    sudo ln $MPT/newlink $MPT/memfile  && fail "ln hard link in fuse should fail"
    sudo ln -s $MPT/slink $MPT/memfile && fail "ln soft link in fuse should fail"
    sudo mknod $MPT/myblk b 100 100    && fail "mknod special file should fail in fuse"
    sudo rmdir $MPT/tmpdir             && fail "rmdir should fail in fuse"
    sudo rm $MPT/memfile               && fail "rm file in fuse should fail"
    sudo touch $MPT/touchfile          && fail "touch new file in fuse should fail"
elif [[ "${FAMFS_MODE}" == "v1" && "$major" -ge 6 && "$minor" -ge 12 ]]; then
    # Stuff that should fail
    sudo truncate --size 0 $MPT/memfile     && fail "truncate famfsv1 file should fail"
    sudo ln $MPT/newlink $MPT/memfile  && fail "ln hard link in famfsv1 should fail"
    sudo ln -s $MPT/slink $MPT/memfile && fail "ln soft link in famfsv1 should fail"
    sudo mknod $MPT/myblk b 100 100    && fail "mknod special file should fail in famfsv1"
    sudo rmdir $MPT/tmpdir             && fail "rmdir should fail in famfsv1"
    sudo rm $MPT/memfile               && fail "rm file in famfsv1 should fail"
else
    echo "test_shadow_yaml: skipping some tests due to older famfs and kernel"
fi

echo 3 | sudo tee /proc/sys/vm/drop_caches

# work thorugh permissions of different mount types here
#find $FUSE_MPT || fail "Find to recursively list fuse should succeed"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_shadow_yaml.shadow $MPT

set +x
echo "*************************************************************************"
echo "$TEST completed successfully"
echo "*************************************************************************"
exit 0
