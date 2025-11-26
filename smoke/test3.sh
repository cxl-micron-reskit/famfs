#!/usr/bin/env bash

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

TEST="test3"
start_test $TEST

#set -x

# Start with a clean, empty file system
famfs_recreate "test3"

verify_mounted "$DEV" "$MPT" "test3.sh"

expect_good "${CLI[@]}" creat -r -s 4096 -S 1 "$MPT/ddtest" \
           -- "creat ddfile"
expect_good "${CLI[@]}" verify -S 1 -f "$MPT/ddtest" \
           -- "verify ddfile creat"
expect_good "${CLI[@]}" cp "$MPT/ddtest" "$MPT/ddtest_copy" \
            -- "copy ddfile should succeed"
# This will overwrite the seeded contents of ddfile
expect_good sudo dd if=/dev/zero of="$MPT/ddtest" bs=4096 count=1 conv=notrunc \
           -- "dd into ddfile"
expect_fail "${CLI[@]}" verify -S 1 -f "$MPT/ddtest" \
           -- "verify should fail after dd overwrite"
expect_good sudo dd of=/dev/null if="$MPT/ddtest" bs=4096 \
           -- "dd out of ddfile"

#
# Test some cases where the kmod should throw errors because the famfs file is
# not in a valid state
#
set +e
sudo truncate "$MPT/ddtest" -s 2048
rc="$?"
set -e
if (( rc == 0 )); then
    # This should be reconsidered when we no longer support kmods that
    # allow truncate XXX
    echo "--------------------------------------------"
    echo "This kernel allows truncate"
    echo "--------------------------------------------"
    assert_file_size "$MPT/ddtest" 2048 "bad size after rogue truncate"
    expect_fail sudo dd of=/dev/null if="$MPT/ddtest" bs=2048 \
        -- "Any read from a truncated file should fail"
    expect_fail sudo truncate "$MPT/ddtest" -s 4096 \
        -- "truncate extra-hinky - back to original size"
    assert_file_size "$MPT/ddtest" 4096 "bad size after second rogue truncate"
    expect_fail sudo dd of=/dev/null if="$MPT/ddtest" bs=2048 \
        -- "Read from previously horked file should fail"
fi

# Test behavior of standard "cp" into famfs
# The create should succeed, but the write should fail, leaving an empty, invalid file
# (Inline "expect_fail" semantics here to avoid mysterious early exits.)
set +e
echo ":== sudo cp /etc/passwd $MPT/pwd"
sudo cp /etc/passwd "$MPT/pwd"
cp_status=$?
set -e
if (( cp_status == 0 )); then
    fail "cp to famfs should fail due to invalid famfs metadata: command succeeded unexpectedly"
else
    echo ":== cp to famfs: good failure (cp to famfs should fail due to invalid famfs metadata, exit $cp_status)"
fi

if [[ "${FAMFS_MODE}" == "v1" ]]; then
    expect_good test -f "$MPT/pwd" \
               -- "v1 cp should leave an invalid destination file"

    # Inline expect_fail for: test -s $MPT/pwd (we expect it to FAIL, i.e., file is empty)
    set +e
    echo ":== test -s $MPT/pwd"
    test -s "$MPT/pwd"
    ts_status=$?
    set -e
    if (( ts_status == 0 )); then
        fail "file from cp should be empty: test -s succeeded (file is non-empty)"
    else
        echo ":== test -s: good failure (file from cp should be empty, exit $ts_status)"
    fi

    # Create an invalid file via "touch" and test behavior
    expect_good sudo touch "$MPT/touchfile" \
               -- "touch should succeed at creating an invalid file"

    # Inline expect_fail for the dd read-from-invalid-file case
    set +e
    echo ":== sudo dd if='$MPT/touchfile' of=/dev/null"
    sudo dd if="$MPT/touchfile" of=/dev/null bs=4096 count=1
    dd_status=$?
    set -e
    if (( dd_status == 0 )); then
	fail "dd from invalid file should fail: command unexpectedly succeeded"
    else
	echo ":== dd: good failure (dd from invalid file should fail, exit $dd_status)"
    fi

    set +e
    sudo truncate "$MPT/touchfile" -s 8192
    rc="$?"
    set -e
    if (( "$rc" == 0 )); then
        # This should be reconsidered when we no longer support kmods that
        # allow truncate XXX
        expect_fail sudo dd if="$MPT/touchfile" of=/dev/null bs=8192 count=1 \
            -- "dd from touchfile should fail"
        expect_fail sudo dd if=/dev/zero of="$MPT/touchfile" bs=8192 count=1 \
            -- "dd to touchfile should fail"
    fi
else
    expect_fail test -f "$MPT/pwd" \
               -- "non-cli cp to famfs/fuse should fail outright"
    # Create an invalid file via "touch" and test behavior
    expect_fail sudo touch "$MPT/touchfile" \
               -- "non-cli touch should fail in famfs/fuse"
    expect_fail sudo dd if="$MPT/touchfile" \
               -- "dd from missing file should fail"
fi

expect_good stat "$MPT/ddtest" -- "stat ddtest should work"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    expect_good sudo curl \
        --unix-socket "$(scripts/famfs_shadow.sh "$MPT")/sock" \
        http://localhost/icache_stats \
        -- "icache_stats"
fi

# unmount and remount
expect_good sudo "$UMOUNT" "$MPT" -- "umount"

# findmnt may legitimately fail if nothing is mounted; don't let set -e kill us
set +e
findmnt -t famfs
set -e

verify_not_mounted "$DEV" "$MPT" "test3"
sleep 1

expect_good "${MOUNT[@]}" "$DEV" "$MPT" -- "remount test3"
verify_mounted "$DEV" "$MPT" "test3 x"

set +e
findmnt -t famfs
set -e

expect_good sudo stat "$MPT/ddtest" -- "stat ddtest after remount should work"

# Test that our invalid files from above are gone after umount/mount

#
# Note shell builtins don't work well with expect_fail() etc.
sudo test -f "$MPT/touchfile" && fail "touchfile should have disappeared"
sudo test -f "$MPT/pwd" && fail "pwd file should have disappeared"
sudo test -f "$MPT/ddtest" || "ddtest file should have reappeared and become valid again"

# Unmounting and remounting the file system should have restored the ddtest file's
# size after the rogue truncate above. Double check this
assert_file_size "$MPT/ddtest" 4096 "bad file size after remount"

expect_fail "${CLI[@]}" verify -S 1 -f "$MPT/ddtest" \
           -- "verify ddfile should fail since it was overwritten"
expect_good sudo dd conv=notrunc if="$MPT/ddtest_copy" of="$MPT/ddtest" bs=2048 \
           -- "dd contents back into ddfile"
expect_good "${CLI[@]}" verify -S 1 -f "$MPT/ddtest" \
           -- "verify ddfile should succeed since contents put back"

expect_good "${FSCK[@]}" "$MPT" -- "fsck should succeed - no cross links yet"

expect_good sudo mkdir -p /tmp/smoke.shadow/test3.shadow/root \
           -- "mkdir shadow"
expect_good "${CLI[@]}" logplay -Ss /tmp/smoke.shadow/test3.shadow "$MPT" \
           -- "test3 shadow logplay"

set +x
finish_test $TEST
exit 0
