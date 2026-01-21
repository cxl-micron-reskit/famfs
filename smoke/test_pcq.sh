#!/usr/bin/env bash

set -euo pipefail

TEST="pcq"

source smoke/test_header.sh
source "$SCRIPTS/test_funcs.sh"

start_test $TEST

STATUSFILE="/tmp/pcqstatus_$$.txt"

# Command arrays
PCQ=(sudo "${VG[@]}" "$BIN/pcq")
pcq=("${VG[@]}" "$BIN/pcq")

# Start with a clean, empty file system
famfs_recreate "test_pcq"

id=$(id -un)
grp=$(id -gn)
uargs=(-u "$id" -g "$grp")

# Test some bogus command line combinations
expect_good "${PCQ[@]}" -? \
           -- "pcq help should work"
expect_fail "${PCQ[@]}" --info --create \
           -- "pcq should fail with --info and --create"
expect_fail "${PCQ[@]}" --drain --producer \
           -- "pcq should fail with --drain and --producer"
expect_fail "${PCQ[@]}" --time 10 --nmessages 100 \
           -- "pcq should fail with --time and --nmessages"
expect_fail "${PCQ[@]}" --create \
           -- "pcq --create should fail with no bsize"
expect_fail "${PCQ[@]}" --create -D -v --bsize 1024 --nbuckets 1024 \
           -- "pcq create should fail with missing filename"
expect_fail "${PCQ[@]}" --create -D -v --bsize 1024 --nbuckets 1024 \
           -- "Create should fail with no file"

# Create some queues
expect_good "${PCQ[@]}" --create "${uargs[@]}" -D -v --bsize 1024  --nbuckets 1024 "$MPT/q0" \
           -- "basic pcq create 0"
expect_good "${PCQ[@]}" --create "${uargs[@]}" -v --bsize 64   --nbuckets 1K   "$MPT/q1" \
           -- "basic pcq create 1"
expect_good "${PCQ[@]}" --create "${uargs[@]}" -v --bsize 64K  --nbuckets 512  "$MPT/q2" \
           -- "basic pcq create 2"
expect_good "${PCQ[@]}" --create "${uargs[@]}" -v --bsize 512K --nbuckets 1k   "$MPT/q3" \
           -- "basic pcq create 3"
expect_good "${PCQ[@]}" --create "${uargs[@]}" -v --bsize 256K --nbuckets 256  "$MPT/q4" \
           -- "basic pcq create 4"

# Set ownership to the non-privileged caller
expect_good sudo chown "$id:$grp" "$MPT/q0" -- "chown q0"
expect_good sudo chown "$id:$grp" "$MPT/q1" -- "chown q1"
expect_good sudo chown "$id:$grp" "$MPT/q2" -- "chown q2"
expect_good sudo chown "$id:$grp" "$MPT/q3" -- "chown q3"
expect_good sudo chown "$id:$grp" "$MPT/q4" -- "chown q4"
expect_good sudo chown "$id:$grp" "$MPT/q0.consumer" -- "chown q0.consumer"
expect_good sudo chown "$id:$grp" "$MPT/q1.consumer" -- "chown q1.consumer"
expect_good sudo chown "$id:$grp" "$MPT/q2.consumer" -- "chown q2.consumer"
expect_good sudo chown "$id:$grp" "$MPT/q3.consumer" -- "chown q3.consumer"
expect_good sudo chown "$id:$grp" "$MPT/q4.consumer" -- "chown q4.consumer"

# From here on we run the non-sudo ${pcq} rather than the sudo ${PCQ}

# Coverage tests leave some root cruft after running pcq as root; clean that up...
sudo chown -R "${id}:${grp}" "$BIN"

# Test setperm
# XXX: --setperm not working yet on fuse as of 9/2025
if [[ "${FAMFS_MODE}" == "v1" ]]; then
    # Consumer permission
    expect_good "${pcq[@]}" --setperm c "$MPT/q0" \
               -- "setperm c should work"
    expect_fail test -w "$MPT/q0" \
               -- "setperm c should remove write permission on q0"
    expect_good test -w "$MPT/q0.consumer" \
               -- "setperm c should restore write permission on q0.consumer"
    expect_good "${pcq[@]}" --info -v "$MPT/q0" \
               -- "pcq info perm c should work"
    expect_fail "${pcq[@]}" --producer -v -N 1 "$MPT/q0" \
               -- "producer should fail with c permission on q0"

    # Producer permission
    expect_good "${pcq[@]}" --setperm p "$MPT/q0" \
               -- "setperm p should work"
    expect_good test -w "$MPT/q0" \
               -- "setperm p should restore write permission on q0"
    expect_fail test -w "$MPT/q0.consumer" \
               -- "setperm p should remove write permission on q0.consumer"
    expect_good "${pcq[@]}" --info -v "$MPT/q0" \
               -- "pcq info perm p should work"
    expect_fail "${pcq[@]}" --consumer -N 1 -v "$MPT/q0" \
               -- "consumer should fail with p permission on q0"
    expect_fail "${pcq[@]}" --drain -v "$MPT/q0" \
               -- "drain should fail with p permission on q0"

    # Neither permission
    expect_good "${pcq[@]}" --setperm n "$MPT/q0" \
               -- "setperm n should work"
    expect_fail test -w "$MPT/q0" \
               -- "setperm n should remove write permission on q0"
    expect_fail test -w "$MPT/q0.consumer" \
               -- "setperm n should remove write permission on q0.consumer"
    expect_good "${pcq[@]}" --info -v "$MPT/q0" \
               -- "pcq info perm n should work"
    expect_fail "${pcq[@]}" --producer -v -N 1 "$MPT/q0" \
               -- "producer should fail with n permission on q0"
    expect_fail "${pcq[@]}" --consumer -N 1 -v "$MPT/q0" \
               -- "consumer should fail with n permission on q0"

    # Both producer and consumer permission
    expect_good "${pcq[@]}" --setperm b "$MPT/q0" \
               -- "setperm b should work"
    expect_good test -w "$MPT/q0" \
               -- "setperm b should restore write permission on q0"
    expect_good test -w "$MPT/q0.consumer" \
               -- "setperm b should restore write permission on q0.consumer"
    expect_good "${pcq[@]}" --info -v "$MPT/q0" \
               -- "pcq info perm b should work"
fi

expect_good "${pcq[@]}" --info -v "$MPT/q0" \
           -- "basic pcq info 0"
expect_good "${pcq[@]}" --info -v "$MPT/q1" \
           -- "basic pcq info 1"
expect_good "${pcq[@]}" --info -v "$MPT/q2" \
           -- "basic pcq info 2"
expect_good "${pcq[@]}" --info -v "$MPT/q3" \
           -- "basic pcq info 3"
expect_good "${pcq[@]}" --info -v "$MPT/q4" \
           -- "basic pcq info 4"

expect_good "${pcq[@]}" --drain -v "$MPT/q0" \
           -- "drain empty q0"
expect_good "${pcq[@]}" --drain -v "$MPT/q1" \
           -- "drain empty q1"
expect_good "${pcq[@]}" --drain -v "$MPT/q2" \
           -- "drain empty q2"
expect_good "${pcq[@]}" --drain -v "$MPT/q3" \
           -- "drain empty q3"
expect_good "${pcq[@]}" --drain -v "$MPT/q4" \
           -- "drain empty q4"

# put 128 entries in each queue
expect_good "${pcq[@]}" --producer -v -N 128 --statusfile "$STATUSFILE" "$MPT/q0" \
           -- "put 128 in q0"
assert_equal "$(cat "$STATUSFILE")" 128 "put 128 in q0"

expect_good "${pcq[@]}" --producer -v -N 128 --statusfile "$STATUSFILE" "$MPT/q1" \
           -- "put 128 in q1"
assert_equal "$(cat "$STATUSFILE")" 128 "put 128 in q1"

expect_good "${pcq[@]}" --producer -v -N 128 --statusfile "$STATUSFILE" "$MPT/q2" \
           -- "put 128 in q2"
assert_equal "$(cat "$STATUSFILE")" 128 "put 128 in q2"

expect_good "${pcq[@]}" --producer -v -N 128 --statusfile "$STATUSFILE" "$MPT/q3" \
           -- "put 128 in q3"
assert_equal "$(cat "$STATUSFILE")" 128 "put 128 in q3"

expect_good "${pcq[@]}" --producer -v -N 128 --statusfile "$STATUSFILE" "$MPT/q4" \
           -- "put 128 in q4"
assert_equal "$(cat "$STATUSFILE")" 128 "put 128 in q4"

# consume half of the elements from each queue
expect_good "${pcq[@]}" --consumer -N 64 -v --statusfile "$STATUSFILE" "$MPT/q0" \
           -- "consume 64 from q0"
assert_equal "$(cat "$STATUSFILE")" 64 "consume 64 from q0"

expect_good "${pcq[@]}" --consumer -N 64 -v --statusfile "$STATUSFILE" "$MPT/q1" \
           -- "consume 64 from q1"
assert_equal "$(cat "$STATUSFILE")" 64 "consume 64 from q1"

expect_good "${pcq[@]}" --consumer -N 64 -v --statusfile "$STATUSFILE" "$MPT/q2" \
           -- "consume 64 from q2"
assert_equal "$(cat "$STATUSFILE")" 64 "consume 64 from q2"

expect_good "${pcq[@]}" --consumer -N 64 -v --statusfile "$STATUSFILE" "$MPT/q3" \
           -- "consume 64 from q3"
assert_equal "$(cat "$STATUSFILE")" 64 "consume 64 from q3"

expect_good "${pcq[@]}" --consumer -N 64 -v --statusfile "$STATUSFILE" "$MPT/q4" \
           -- "consume 64 from q4"
assert_equal "$(cat "$STATUSFILE")" 64 "consume 64 from q4"

# Drain the rest of the elements
expect_good "${pcq[@]}" --drain -v --statusfile "$STATUSFILE" "$MPT/q0" \
           -- "drain 64 from q0"
assert_equal "$(cat "$STATUSFILE")" 64 "drain 64 from q0"

expect_good "${pcq[@]}" --drain -v --statusfile "$STATUSFILE" "$MPT/q1" \
           -- "drain 64 from q1"
assert_equal "$(cat "$STATUSFILE")" 64 "drain 64 from q1"

expect_good "${pcq[@]}" --drain -v --statusfile "$STATUSFILE" "$MPT/q2" \
           -- "drain 64 from q2"
assert_equal "$(cat "$STATUSFILE")" 64 "drain 64 from q2"

expect_good "${pcq[@]}" --drain -v --statusfile "$STATUSFILE" "$MPT/q3" \
           -- "drain 64 from q3"
assert_equal "$(cat "$STATUSFILE")" 64 "drain 64 from q3"

expect_good "${pcq[@]}" --drain -v --statusfile "$STATUSFILE" "$MPT/q4" \
           -- "drain 64 from q4"
assert_equal "$(cat "$STATUSFILE")" 64 "drain 64 from q4"

# Simultaneous producer/consumer for 1K messages with seed verification
expect_good "${pcq[@]}" -pc --seed 43 -N 1000 --statusfile "$STATUSFILE" "$MPT/q0" \
           -- "p/c 1k in q0"
assert_equal "$(cat "$STATUSFILE")" 2000 "produce/consume 1k with q0"

expect_good "${pcq[@]}" -pc --seed 43 -N 1000 --statusfile "$STATUSFILE" "$MPT/q1" \
           -- "p/c 1k in q1"
assert_equal "$(cat "$STATUSFILE")" 2000 "produce/consume 1k with q1"

expect_good "${pcq[@]}" -pc --seed 43 -N 1000 --statusfile "$STATUSFILE" "$MPT/q2" \
           -- "p/c 1k in q2"
assert_equal "$(cat "$STATUSFILE")" 2000 "produce/consume 1k with q2"

expect_good "${pcq[@]}" -pc --seed 43 -N 1000 --statusfile "$STATUSFILE" "$MPT/q3" \
           -- "p/c 1k in q3"
assert_equal "$(cat "$STATUSFILE")" 2000 "produce/consume 1k with q3"

expect_good "${pcq[@]}" -pc --seed 43 -N 1000 --statusfile "$STATUSFILE" "$MPT/q4" \
           -- "p/c 1k in q4"
assert_equal "$(cat "$STATUSFILE")" 2000 "produce/consume 1k with q4"

# Simultaneous producer/consumer for 10K messages
expect_good "${pcq[@]}" -pc -s 1 -N 10000 --statusfile "$STATUSFILE" "$MPT/q0" \
           -- "p/c 10k in q0"
assert_equal "$(cat "$STATUSFILE")" 20000 "produce/consume 10k with q0"

expect_good "${pcq[@]}" -pc -s 1 -N 10000 --statusfile "$STATUSFILE" "$MPT/q1" \
           -- "p/c 10k in q1"
assert_equal "$(cat "$STATUSFILE")" 20000 "produce/consume 10k with q1"

expect_good "${pcq[@]}" -pc -s 1 -N 10000 --statusfile "$STATUSFILE" "$MPT/q2" \
           -- "p/c 10k in q2"
assert_equal "$(cat "$STATUSFILE")" 20000 "produce/consume 10k with q2"

expect_good "${pcq[@]}" -pc -s 1 -N 10000 --statusfile "$STATUSFILE" "$MPT/q3" \
           -- "p/c 10k in q3"
assert_equal "$(cat "$STATUSFILE")" 20000 "produce/consume 10k with q3"

expect_good "${pcq[@]}" -pc -s 1 -N 10000 --statusfile "$STATUSFILE" "$MPT/q4" \
           -- "p/c 10k in q4"
assert_equal "$(cat "$STATUSFILE")" 20000 "produce/consume 10k with q4"

# Timed runs
echo "10 second run in progress on q0..."
expect_good "${pcq[@]}" -pc --time 10 "$MPT/q0" \
           -- "p/c 10 seconds q0"

echo "10 second run in progress on q1..."
expect_good "${pcq[@]}" -pc -s 1 --time 10 "$MPT/q1" \
           -- "p/c 10 seconds q1"

echo "10 second run in progress on q2..."
expect_good "${pcq[@]}" -pc -s 1 --time 10 "$MPT/q2" \
           -- "p/c 10 seconds q2"

echo "10 second run in progress on q3..."
expect_good "${pcq[@]}" -pc -s 1 --time 10 "$MPT/q3" \
           -- "p/c 10 seconds q3"

echo "10 second run in progress on q4..."
expect_good "${pcq[@]}" -pc -s 1 --time 10 "$MPT/q4" \
           -- "p/c 10 seconds q4"

expect_good "${pcq[@]}" --info -v "$MPT/q0" \
           -- "maybe not empty pcq info 0"
expect_good "${pcq[@]}" --info -v "$MPT/q1" \
           -- "maybe not empty pcq info 1"
expect_good "${pcq[@]}" --info -v "$MPT/q2" \
           -- "maybe not empty pcq info 2"
expect_good "${pcq[@]}" --info -v "$MPT/q3" \
           -- "maybe not empty pcq info 3"
expect_good "${pcq[@]}" --info -v "$MPT/q4" \
           -- "maybe not empty pcq info 4"

# Drain the queues
expect_good "${pcq[@]}" --drain "$MPT/q0" \
           -- "drain q0"
expect_good "${pcq[@]}" --drain "$MPT/q1" \
           -- "drain q1"
expect_good "${pcq[@]}" --drain "$MPT/q2" \
           -- "drain q2"
expect_good "${pcq[@]}" --drain "$MPT/q3" \
           -- "drain q3"
expect_good "${pcq[@]}" --drain "$MPT/q4" \
           -- "drain q4"

expect_good "${pcq[@]}" --info "$MPT/q0" \
           -- "empty pcq info 0"
expect_good "${pcq[@]}" --info "$MPT/q1" \
           -- "empty pcq info 1"
expect_good "${pcq[@]}" --info "$MPT/q2" \
           -- "empty pcq info 2"
expect_good "${pcq[@]}" --info "$MPT/q3" \
           -- "empty pcq info 3"
expect_good "${pcq[@]}" --info "$MPT/q4" \
           -- "empty pcq info 4"

expect_good unlink "$STATUSFILE" \
           -- "failed to unlink $STATUSFILE"

SHADOW="/tmp/smoke.shadow/test_pcq.shadow"
expect_good sudo mkdir -p "$SHADOW/root" \
           -- "failed to mkdir -p $SHADOW/root"
expect_good "${CLI[@]}" logplay -Ss "$SHADOW" "$MPT" \
           -- "pcq logplay -Ss should succeed"

set +x
finish_test $TEST
exit 0
