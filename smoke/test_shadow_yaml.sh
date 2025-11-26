#!/usr/bin/env bash

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

TEST="test_shadow_yaml"
start_test $TEST

#set -x

# Start with a clean, empty file system
famfs_recreate "test_shadow_yaml"

verify_mounted "$DEV" "$MPT" "$TEST"
verify_mounted "$DEV" "$MPT" "$TEST.sh mounted"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    expect_good sudo curl \
        --unix-socket "$(scripts/famfs_shadow.sh "$MPT")/sock" \
        http://localhost/icache_stats \
        -- "icache_stats REST query"
fi

# Unmount
expect_good sudo "$UMOUNT" "$MPT" -- "$TEST.sh umount"
verify_not_mounted "$DEV" "$MPT" "$TEST.sh"

#
# Test shadow logplay while fs is NOT mounted
#
SHADOWPATH=/tmp/shadowpath/root
sudo rm -rf "$SHADOWPATH"

expect_fail "${CLI[@]}" logplay --shadow -d /dev/bogodax -- \
    "shadow logplay should fail with bogus daxdev"

expect_fail "${CLI[@]}" logplay --shadow "$SHADOWPATH/frob" --daxdev "$DEV" -vv -- \
    "shadow logplay to nonexistent shadow dir should fail if parent doesn't exist"

expect_fail "${CLI[@]}" logplay --daxdev "$DEV" "$SHADOWPATH" -vv -- \
    "logplay should fail if --daxdev is set without --shadow"

expect_fail "${CLI[@]}" logplay --shadow /etc/passwd --daxdev "$DEV" -vv -- \
    "shadow logplay to regular file should fail"

# valid shadow dir
sudo rm -rf "$SHADOWPATH"
sudo mkdir -p "$SHADOWPATH"
expect_good "${CLI[@]}" logplay --shadow "$SHADOWPATH" --daxdev "$DEV" -vv -- \
    "shadow logplay to existing shadow dir should succeed"
expect_good "${CLI[@]}" logplay --shadow "$SHADOWPATH" --daxdev "$DEV" -vv -- \
    "redo shadow logplay should succeed"

# shadowtest → re-parse YAML
sudo rm -rf "$SHADOWPATH"/*
expect_good "${CLI[@]}" logplay --shadow "$SHADOWPATH" --shadowtest --daxdev "$DEV" -vv -- \
    "shadow logplay with yaml test should succeed"

# shadow logplay without permissions
sudo rm -rf /tmp/famfs2
sudo mkdir /tmp/famfs2
expect_fail "${CLI_NOSUDO[@]}" logplay --shadow /tmp/famfs2 --daxdev "$DEV" -vv -- \
    "shadow logplay to non-writable shadow dir should fail"
sudo rm -rf /tmp/famfs2

# Remount famfs after shadow tests
expect_good "${MOUNT[@]}" "$DEV" "$MPT" -- "remount after shadow yaml test should work"
verify_mounted "$DEV" "$MPT" "$TEST.sh mounted 2"

#
# Test actual FUSE shadow pathing via famfs_fused
#
FAMFS_FUSED=(sudo "$BIN/famfs_fused")

expect_good "${CLI[@]}" creat -s 3g      "$MPT/memfile"   -- "create memfile"
expect_good "${CLI[@]}" creat -s 100m    "$MPT/memfile1"  -- "creat with -s 100m"
expect_good "${CLI[@]}" creat -s 10000k  "$MPT/memfile2"  -- "creat with -s 10000k"
expect_good "${CLI[@]}" mkdir            "$MPT/tmpdir"    -- "mkdir should succeed"

FUSE_SHADOW="/tmp/s/root"
FUSE_MPT="/tmp/famfs_fuse"
sudo rm -rf "$FUSE_SHADOW"
mkdir -p "$FUSE_SHADOW" "$FUSE_MPT"

expect_good "${CLI[@]}" logplay --shadow "$FUSE_SHADOW" --daxdev "$DEV" -vv -- \
    "shadow logplay to $FUSE_SHADOW should succeed"

expect_good "${FAMFS_FUSED[@]}" --help    -- "famfs_fused --help should succeed"
expect_good "${FAMFS_FUSED[@]}" --version -- "famfs_fused --version should succeed"

# Bad args for fused
expect_fail "${FAMFS_FUSED[@]}" -df -o source="$FUSE_SHADOW" -- \
    "fused should fail w/missing MPT"
expect_fail "${FAMFS_FUSED[@]}" -df "$FUSE_MPT" -- \
    "fused should fail w/missing source"
expect_fail "${FAMFS_FUSED[@]}" -o source=/bad/mpt "$FUSE_MPT" -- \
    "fused should fail w/bad MPT"
expect_fail "${FAMFS_FUSED[@]}" -o source=/etc/passwd "$FUSE_MPT" -- \
    "fused should fail with file as mpt"

#
# Extract kernel major/minor
#
kernel_version=$(uname -r)
if [[ $kernel_version =~ ^([0-9]+)\.([0-9]+) ]]; then
    major=${BASH_REMATCH[1]}
    minor=${BASH_REMATCH[2]}
else
    echo "Error: Unable to parse kernel version: $kernel_version" >&2
    exit 1
fi

echo "Major Version: $major"
echo "Minor Version: $minor"

#
# Negative tests for operations not allowed in fuse/v1
#
if [[ "$FAMFS_MODE" == "fuse" ]]; then

    expect_fail sudo truncate --size 0 "$MPT/memfile"    -- "truncate fuse should fail"
    expect_fail sudo mkdir "$MPT/mydir"                  -- "mkdir in fuse should fail"
    expect_fail sudo ln "$MPT/newlink" "$MPT/memfile"    -- "hardlink fuse should fail"
    expect_fail sudo ln -s "$MPT/slink" "$MPT/memfile"   -- "symlink fuse should fail"
    expect_fail sudo mknod "$MPT/myblk" b 100 100        -- "mknod fuse should fail"
    expect_fail sudo rmdir "$MPT/tmpdir"                 -- "rmdir fuse should fail"
    expect_fail sudo rm "$MPT/memfile"                   -- "rm fuse should fail"
    expect_fail sudo touch "$MPT/touchfile"              -- "touch fuse should fail"

elif [[ "$FAMFS_MODE" == "v1" && "$major" -ge 6 && "$minor" -ge 12 ]]; then

    expect_fail sudo truncate --size 0 "$MPT/memfile"    -- "truncate v1 should fail"
    expect_fail sudo ln "$MPT/newlink" "$MPT/memfile"    -- "hardlink v1 should fail"
    expect_fail sudo ln -s "$MPT/slink" "$MPT/memfile"   -- "symlink v1 should fail"
    expect_fail sudo mknod "$MPT/myblk" b 100 100        -- "mknod v1 should fail"
    expect_fail sudo rmdir "$MPT/tmpdir"                 -- "rmdir v1 should fail"
    expect_fail sudo rm "$MPT/memfile"                   -- "rm v1 should fail"

else
    echo "test_shadow_yaml: skipping some tests due to older famfs and kernel"
fi

#
# Drop caches for consistency
#
echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null

# TODO: test permissions / recursive find in fuse mount

set +x
finish_test $TEST
exit 0
