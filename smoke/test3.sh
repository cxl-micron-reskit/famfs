#!/usr/bin/env bash

source smoke/test_header.sh

TEST="test3"

source $SCRIPTS/test_funcs.sh

#set -x

# Start with a clean, empty file systeem
famfs_recreate "test3"

verify_mounted $DEV $MPT "test2.sh"

expect_good ${CLI} creat -r -s 4096 -S 1 $MPT/ddtest    -- "creat ddfile"
expect_good ${CLI} verify -S 1 -f $MPT/ddtest           -- "verify ddfile creat"
expect_good ${CLI} cp $MPT/ddtest $MPT/ddtest_copy      -- "copy ddfile should succeed"
expect_good sudo dd if=/dev/zero of=$MPT/ddtest bs=4096 count=1 conv=notrunc  -- "dd into ddfile"
expect_fail ${CLI} verify -S 1 -f $MPT/ddtest           -- "verify should fail after dd overwrite"
expect_good sudo dd of=/dev/null if=$MPT/ddtest bs=4096 -- "dd out of ddfile"

#
# Test some cases where the kmod should throw errors because the famfs file is
# not in a valid state
#
set +e
sudo truncate $MPT/ddtest -s 2048
rc="$?"
set -e
if (( "$RC" == 0 )); then
    # This should be reconsidered when we no longer support kmods that
    # allow truncate XXX
    echo "--------------------------------------------"
    echo "This kernel allows truncate"
    echo "--------------------------------------------"
    assert_file_size "$MPT/ddtest" 2048 "bad size after rogue truncate"
    expect_fail sudo dd of=/dev/null if=$MPT/ddtest bs=2048 \
	-- "Any read from a truncated file should fail"
    expect_fail sudo truncate $MPT/ddtest -s 4096           \
	-- "truncate extra-hinky - back to original size"
    assert_file_size "$MPT/ddtest"  4096 "bad size after second rogue truncate"
    expect_fail sudo dd of=/dev/null if=$MPT/ddtest bs=2048 \
	-- "Read from previously horked file should fail"
fi

# Test behavior of standard "cp" into famfs
# The create should succeed, but the write should fail, leaving an empty, invalid file
expect_fail sudo cp /etc/passwd $MPT/pwd -- "cp to famfs should fail due to invalid famfs metadata"
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    expect_good test -f $MPT/pwd -- "v1 cp should leave an invalid destination file"
    expect_fail test -s $MPT/pwd -- "file from cp should be empty"
    # Create an invalid file via "touch" and test behavior
    expect_good sudo touch $MPT/touchfile -- "touch should succeed at creating an invalid file"
    expect_fail sudo dd if=$MPT/touchfile -- "dd from invalid file should fail"

    set +e
    sudo truncate $MPT/touchfile -s 8192
    rc="$?"
    set -e
    if (( "$RC" == 0 )); then
	# This should be reconsidered when we no longer support kmods that
	# allow truncate XXX
	expect_fail sudo dd if=$MPT/touchfile of=/dev/null bs=8192 count=1  \
	    -- "dd from touchfile should fail"
	expect_fail sudo dd if=/dev/zero of=$MPT/touchfile bs=8192 count=1  \
	    -- "dd to touchfile should fail"
    fi
else
    expect_fail test -f $MPT/pwd -- "non-cli cp to famfs/fuse should fail outright"
    # Create an invalid file via "touch" and test behavior
    expect_fail sudo touch $MPT/touchfile -- "non-cli touch should fail in famfs/fuse"
    expect_fail sudo dd if=$MPT/touchfile -- "dd from missing file should fail"
fi

expect_good stat $MPT/ddtest -- "stat ddtest should work"

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    sudo curl  --unix-socket $(scripts/famfs_shadow.sh /mnt/famfs)/sock \
	 http://localhost/icache_stats
fi

# unmount and remount
expect_good sudo $UMOUNT $MPT -- "umount"
findmnt -t famfs
verify_not_mounted $DEV $MPT "test3"
sleep 1
${MOUNT} $DEV $MPT
verify_mounted $DEV $MPT "test3 x"
findmnt -t famfs

sudo stat $MPT/ddtest

# Test that our invalid files from above are going after umount/mount
expect_fail sudo test -f $MPT/touchfile           -- "touchfile should have disappeared"
expect_fail sudo test -f $MPT/pwd                 -- "pwd file should have disappeared"
expect_good sudo test -f $MPT/ddtest              -- "ddtest file should have reappeared and become valid again"

# Unmounting and remounting the file system should have restored the ddtest file's
# size after the rogue truncate above. Double check this

assert_file_size "$MPT/ddtest" 4096 "bad file size after remount"

expect_fail ${CLI} verify -S 1 -f $MPT/ddtest -- "verify ddfile should fail since it was overwritten"
expect_good sudo dd conv=notrunc if=$MPT/ddtest_copy of=$MPT/ddtest bs=2048 -- "dd contents back into ddfile"
expect_good ${CLI} verify -S 1 -f $MPT/ddtest -- "verify ddfile should succeed since contents put back"

expect_good ${FSCK} $MPT -- "fsck should succeed - no cross links yet"

expect_good sudo mkdir -p /tmp/smoke.shadow/test3.shadow/root -- "mkdir shadow"
expect_good ${CLI} logplay -Ss ~/smoke.shadow/test3.shadow $MPT \
	    -- "test3 shadow logplay"

set +x
echo ":==*************************************************************************"
echo ":==test3 completed successfully"
echo ":==*************************************************************************"
exit 0
