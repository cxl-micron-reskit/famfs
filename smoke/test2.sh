#!/usr/bin/env bash

source smoke/test_header.sh

TEST="test2"

source "$SCRIPTS/test_funcs.sh"

#set -x

# Start with a clean, empty file system
famfs_recreate "test2"

verify_mounted "$DEV" "$MPT" "test2.sh"
expect_good "${FSCK[@]}" "$MPT" -- "fsck should succeed"

# Try to create a file that is not in a famfs file system (assume relative path not in one)
NOT_IN_FAMFS=no_leading_slash
expect_fail "${CLI[@]}" creat -s 0x400000 "$NOT_IN_FAMFS" \
    -- "creating file not in famfs file system should fail"

# famfs getmap tests (v1 only)
LOG="$MPT/.meta/.log"
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    expect_good "${CLI[@]}" getmap -h -- "getmap -h should succeed"
    expect_fail "${CLI[@]}" getmap   -- "getmap with no file arg should fail"
    expect_fail "${CLI[@]}" getmap badfile -- "getmap on nonexistent file should fail"
    expect_fail "${CLI[@]}" getmap -c badfile -- "getmap -c on nonexistent file should fail"
    expect_fail "${CLI[@]}" getmap /etc/passwd -- "getmap on non-famfs file should fail"

    expect_good "${CLI[@]}" getmap "$LOG" -- "getmap should succeed on famfs log file"
    expect_good "${CLI[@]}" getmap -q "$LOG" -- "getmap -q should succeed on famfs log file"

    # Should fail on nonexistent file inside famfs
    NOT_EXIST="$MPT/not_exist"
    expect_fail "${CLI[@]}" getmap "$NOT_EXIST" \
        -- "getmap should fail on nonexistent file inside famfs"

    # Should fail on file not in famfs
    expect_fail "${CLI[@]}" getmap "$NOT_IN_FAMFS" \
        -- "getmap should fail if file not in famfs"
fi

F=bigtest
SIZE=0x4000000
for N in 10 11 12 13 14 15; do
    FILE="${F}${N}"
    expect_good "${CLI[@]}" creat -r -S "$N" -s "$SIZE" "$MPT/$FILE" \
        -- "creat $FILE"
    expect_good "${CLI[@]}" verify -S "$N" -f "$MPT/$FILE" \
        -- "$FILE mismatch"
done

for N in 10 11 12 13 14 15; do
    FILE="${F}${N}"
    expect_good "${CLI[@]}" verify -S "$N" -f "$MPT/$FILE" \
        -- "$FILE mismatch"
done

# Dump icache stats before umount
if [[ "$FAMFS_MODE" == "fuse" ]]; then
    # turn up log debug
    expect_good sudo curl \
        --unix-socket "$(scripts/famfs_shadow.sh "$MPT")/sock" \
        http://localhost/icache_stats \
        -- "icache_stats"
fi

expect_good sudo "$UMOUNT" "$MPT" -- "umount"
verify_not_mounted "$DEV" "$MPT" "test2.sh"

expect_good "${MOUNT[@]}" "$DEV" "$MPT" -- "mount should succeed"
verify_mounted "$DEV" "$MPT" "test2.sh"

expect_fail sudo cmp "$MPT/bigtest10" "$MPT/bigtest11" \
    -- "files should not match"

expect_good sudo mkdir -p /tmp/smoke.shadow/test2.shadow/root
expect_good "${CLI[@]}" logplay -Ss /tmp/smoke.shadow/test2.shadow "$MPT"

set +x
echo ":==*************************************************************************"
echo ":==test2 completed successfully"
echo ":==*************************************************************************"
exit 0
