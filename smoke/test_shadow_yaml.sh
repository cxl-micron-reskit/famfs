#!/usr/bin/env bash

source ./test_header.sh

TEST="test_shadow_yaml"

source $SCRIPTS/test_funcs.sh

set -x

# Start with a clean, empty file systeem
famfs_recreate "test_shadow_yaml"

verify_mounted $DEV $MPT $TEST

verify_mounted $DEV $MPT "$TEST.sh mounted"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
	 http://localhost/icache_stats
fi

expect_good sudo $UMOUNT $MPT -- "$TEST.sh umount"
verify_not_mounted $DEV $MPT "$TEST.sh"

# Test shadow logplay while the fs is not mounted
SHADOWPATH=/tmp/shadowpath/root
expect_fail ${CLI} logplay --shadow -d /dev/bogodax -- "shadow logplay should fail with bogus daxdev"
sudo rm -rf $SHADOWPATH
expect_fail ${CLI} logplay --shadow $SHADOWPATH/frob --daxdev $DEV -vv   -- \
    "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"
expect_fail ${CLI} logplay --daxdev $DEV $SHADOWPATH -vv -- \
    "logplay should fail if --daxdev is set without --shadow"
expect_fail ${CLI} logplay --shadow /etc/passwd --daxdev $DEV -vv  -- \
    "shadow logplay to regular file should fail"

sudo rm -rf $SHADOWPATH
sudo mkdir -p $SHADOWPATH
expect_good ${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv  -- \
    "shadow logplay to existing shadow dir should succeed"
expect_good ${CLI} logplay --shadow $SHADOWPATH --daxdev $DEV -vv -- \
    "redo shadow logplay to existing shadow dir should succeed"

# --shadowtest arg means re-parse yaml to test
# (if shadow the files are not already present)
sudo rm -rf $SHADOWPATH/*
expect_good ${CLI} logplay --shadow $SHADOWPATH --shadowtest --daxdev $DEV  -vv  -- \
    "shadow logplay with yaml test to existing shadow dir should succeed"

sudo rm -rf /tmp/famfs2
sudo mkdir /tmp/famfs2
expect_fail ${CLI_NOSUDO} logplay --shadow /tmp/famfs2 --daxdev $DEV -vv -- \
    "shadow logplay to non-writable shadow dir should fail"
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

expect_good ${MOUNT} $DEV $MPT -- "remount after shadow yaml test should work"
verify_mounted $DEV $MPT "$TEST.sh mounted 2"

# TODO: move this to new smoke/test_fused.sh
FAMFS_FUSED="sudo $BIN/famfs_fused"

expect_good ${CLI} creat -s 3g  ${MPT}/memfile       -- "can't create memfile"
expect_good ${CLI} creat -s 100m ${MPT}/memfile1     -- "creat should succeed with -s 100m"
expect_good ${CLI} creat -s 10000k ${MPT}/memfile2   -- "creat with -s 10000k should succeed"
expect_good ${CLI} mkdir ${MPT}/tmpdir -- "mkdir should succeed"
FUSE_SHADOW="/tmp/s/root"

FUSE_MPT="/tmp/famfs_fuse"
sudo rm -rf $FUSE_SHADOW
mkdir -p $FUSE_SHADOW $FUSE_MPT

expect_good ${CLI} logplay --shadow $FUSE_SHADOW --daxdev $DEV  -vv -- \
    "shadow logplay to ${FUSE_SHADOW} should succeed"

expect_good ${FAMFS_FUSED} --help -- "famfs_fused --help should succeed"
expect_good ${FAMFS_FUSED} --version -- "famfs_fused --version should succeed"

# Test some bad args
expect_fail ${FAMFS_FUSED} -df -o source=${FUSE_SHADOW} -- "fused should fail w/missing MPT"
expect_fail ${FAMFS_FUSED} -df  $FUSE_MPT -- "fused should fail w/missing source"
expect_fail ${FAMFS_FUSED} -o source=/bad/mpt $FUSE_MPT -- "fused should fail w/bad MPT"
expect_fail ${FAMFS_FUSED} -o source=/etc/passwd $FUSE_MPT -- "fused should fail w/file as mpt"
#expect_fail ${FAMFS_FUSED} -o source=${FUSE_SHADOW} -o foo=bar $FUSE_MPT -- \
#    "fused should fail with bad -o opt (-o foo=bar)"

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
    expect_fail sudo truncate --size 0 $MPT/memfile     -- "truncate fuse file should fail"
    expect_fail sudo mkdir $MPT/mydir                   -- "mkdir should fail in fuse"
    expect_fail sudo ln $MPT/newlink $MPT/memfile  -- "ln hard link in fuse should fail"
    expect_fail sudo ln -s $MPT/slink $MPT/memfile -- "ln soft link in fuse should fail"
    expect_fail sudo mknod $MPT/myblk b 100 100    -- "mknod special file should fail in fuse"
    expect_fail sudo rmdir $MPT/tmpdir             -- "rmdir should fail in fuse"
    expect_fail sudo rm $MPT/memfile               -- "rm file in fuse should fail"
    expect_fail sudo touch $MPT/touchfile          -- "touch new file in fuse should fail"
elif [[ "${FAMFS_MODE}" == "v1" && "$major" -ge 6 && "$minor" -ge 12 ]]; then
    # Stuff that should fail
    expect_fail sudo truncate --size 0 $MPT/memfile     -- "truncate famfsv1 file should fail"
    expect_fail sudo ln $MPT/newlink $MPT/memfile  -- "ln hard link in famfsv1 should fail"
    expect_fail sudo ln -s $MPT/slink $MPT/memfile -- "ln soft link in famfsv1 should fail"
    expect_fail sudo mknod $MPT/myblk b 100 100    -- "mknod special file should fail in famfsv1"
    expect_fail sudo rmdir $MPT/tmpdir             -- "rmdir should fail in famfsv1"
    expect_fail sudo rm $MPT/memfile               -- "rm file in famfsv1 should fail"
else
    echo "test_shadow_yaml: skipping some tests due to older famfs and kernel"
fi

echo 3 | sudo tee /proc/sys/vm/drop_caches

# work thorugh permissions of different mount types here
#find $FUSE_MPT || fail "Find to recursively list fuse should succeed"

mkdir -p ~/smoke.shadow
${CLI} logplay --shadow ~/smoke.shadow/test_shadow_yaml.shadow $MPT

set +x
echo ":==*************************************************************************"
echo ":==$TEST completed successfully"
echo ":==*************************************************************************"
exit 0
