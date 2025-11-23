
# This file is not for running, it is for sourcing into other scripts

fail () {
    set +x
    echo
    echo ":==*** Fail ***"
    echo ":==$TEST: $1"
    echo
    exit 1
}


# Expect the command to succeed (exit 0). Fail on non-zero exits and on crashes (signals).
# Usage:
#   expect_success <command> [args...]
#   expect_success <command> [args...] -- <failure message...>
#
# Examples:
#   expect_success ./build.sh
#   expect_success sudo mkdir -p "$MPT" -- "mkdir should succeed with privileges"
expect_good () {
    local -a cmd=()
    local msg=""
    local saw_delim=0

    # Parse args: before `--` => command; after `--` => message
    for arg in "$@"; do
        if [[ $saw_delim -eq 0 && $arg == "--" ]]; then
            saw_delim=1
            continue
        fi
        if [[ $saw_delim -eq 0 ]]; then
            cmd+=("$arg")
        else
            if [[ -z $msg ]]; then
                msg="$arg"
            else
                msg="$msg $arg"
            fi
        fi
    done

    # Default message if none provided
    [[ -z $msg ]] && msg="expected success"

    # Ensure a command is provided
    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to expect_success"
    fi

    # Run command and capture exit status
    set +e
    echo ":== ${cmd[@]}"
    "${cmd[@]}"
    local status=$?
    set -e

    # Classify outcome
    if (( status == 0 )); then
        # Success as expected
	echo ":== expect good: good completion"
        return 0
    elif (( status >= 128 )); then
        # Terminated by a signal: exit = 128 + signo
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        fail ":== expect_good: ${msg}: bad failure (terminated by ${signame} [$signo], exit=$status)"
    else
        # Regular non-zero exit: unexpected failure
        fail "expect_good: ${msg}: unexpected exit code $status"
    fi
    echo ":== expect_good: good status: ${cmd[@]}"
}

# Run a command that is expected to fail; flag crashes as bad failures.
# Usage patterns:
#   expect_fail <command> [args...]
#   expect_fail <command> [args...] -- <failure message...>
#
# Examples:
#   expect_fail sudo mkdir -p "$MPT" -- "mkdir should fail without privileges"
#   expect_fail ./my_test_binary --bad-input
expect_fail () {
    local -a cmd=()
    local msg=""
    local saw_delim=0

    # Parse args: everything before `--` is the command; after `--` is the message
    for arg in "$@"; do
        if [[ $saw_delim -eq 0 && $arg == "--" ]]; then
            saw_delim=1
            continue
        fi
        if [[ $saw_delim -eq 0 ]]; then
            cmd+=("$arg")
        else
            # accumulate message with spaces preserved
            if [[ -z $msg ]]; then
                msg="$arg"
            else
                msg="$msg $arg"
            fi
        fi
    done

    # If no message provided, use a default
    if [[ -z $msg ]]; then
        msg="expected failure"
    fi

    # Safety: ensure we have a command
    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to expect_fail"
    fi

    # Run the command and capture exit status (including signal-derived statuses)
    set +e
    echo ":== ${cmd[@]}"
    "${cmd[@]}"
    local status=$?
    set -e

    if (( status == 0 )); then
        fail "${msg}: command succeeded unexpectedly"
    elif (( status >= 128 )); then
        # Terminated by a signal: exit = 128 + signo
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        fail "${msg}: bad failure (terminated by ${signame} [$signo], exit=$status)"
    else
        echo ":== expect_fail: good failure (${msg}, exit $status)"
        return 0
    fi
}


# Run a command that is expected to fail; flag crashes and selected exit codes as bad failures.
# Usage:
#   expect_fail_except [--bad CODE|-b CODE]... <command> [args...] [-- <failure message...>]
#
# Examples:
#   expect_fail_except --bad 2 sudo mkdir -p "$MPT" -- "mkdir should fail, but not with exit 2"
#   expect_fail_except -b 2 -b 7 ./my_test_binary --bad-input -- "exit 2/7 are unacceptable"
expect_fail_except () {
    local -a bad_codes=()
    local -a cmd=()
    local msg=""
    local saw_delim=0

    # Parse options and command
    while (($#)); do
        case "$1" in
            -b|--bad)
                shift
                if [[ -z "$1" ]]; then
                    fail "expected a numeric exit code after --bad/-b"
                fi
                # Validate numeric and range 0..128 (you can tighten to 1..127 if you prefer)
                if ! [[ "$1" =~ ^[0-9]+$ ]] || (( $1 < 0 || $1 > 128 )); then
                    fail "invalid --bad code '$1' (expected integer in 0..128)"
                fi
                bad_codes+=("$1")
                shift
                ;;
            --)
                saw_delim=1
                shift
                break
                ;;
            *)
                cmd+=("$1")
                shift
                ;;
        esac
    done

    # Remaining args (if any) form the message
    if (( $# > 0 )); then
        msg="$*"
    fi
    if [[ -z $msg ]]; then
        msg="expected failure"
    fi

    # Safety: ensure we have a command
    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to expect_fail_except"
    fi

    # Execute command, capture status
    set +e
    echo ":== expect_fail_except(${bad_codes[@]}) ${cmd[@]}"
    "${cmd[@]}"
    local status=$?
    set -e

    # Helper: check membership in bad_codes
    local is_bad_code=0
    for bc in "${bad_codes[@]}"; do
        if (( status == bc )); then
            is_bad_code=1
            break
        fi
    done

    if (( status == 0 )); then
        fail "${msg}: command succeeded unexpectedly"
    elif (( status >= 128 )); then
        # Terminated by a signal: exit = 128 + signo
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        fail "${msg}: bad failure (terminated by ${signame} [$signo], exit=$status)"
    elif (( is_bad_code )); then
        fail "${msg}: bad failure (unacceptable exit code $status)"
    else
        echo ":== expect_fail_except(${bad_codes[@]}): good failure (${msg}, exit $status)"
        return 0
    fi
}

# Run a command and only stop (call fail) if it crashes due to a signal.
# Usage:
#   stop_on_crash <command> [args...]
#   stop_on_crash <command> [args...] -- <failure message...>
#
# Examples:
#   stop_on_crash ./prog --option foo
#   stop_on_crash ./prog --input bad.json -- "bad failure: program crashed"
stop_on_crash () {
    local -a cmd=()
    local msg=""
    local saw_delim=0

    # Parse args: before `--` => command; after `--` => message
    for arg in "$@"; do
        if [[ $saw_delim -eq 0 && $arg == "--" ]]; then
            saw_delim=1
            continue
        fi
        if [[ $saw_delim -eq 0 ]]; then
            cmd+=("$arg")
        else
            if [[ -z $msg ]]; then
                msg="$arg"
            else
                msg="$msg $arg"
            fi
        fi
    done

    # Default message if none provided
    if [[ -z $msg ]]; then
        msg="bad failure"
    fi

    # Ensure we have a command
    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to stop_on_crash"
    fi

    # Run command and capture exit status (including signal-derived statuses)
    set +e
    echo ":==${cmd[@]}"
    "${cmd[@]}"
    local status=$?
    set -e

    # Only stop on signal-based termination: shells use 128 + signal_number
    if (( status >= 128 )); then
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        fail "${msg}: terminated by ${signame} [$signo], exit=$status"
    fi

    echo ":== stop_on_crash: command did not crash: ${cmd[@]}"
    # For non-signal exits (including success and regular errors), just return the status.
    # Caller can ignore or use it as needed.
    return 0
}

fail_fsck () {
    set +x
    echo
    echo ":==*** Fail with fsck ***"
    echo ":==$TEST: $1"
    # $2, if provided, is fsck args (e.g. -B 21)
    ${CLI} fsck -v $2 $MPT
    echo ":==$TEST: $1"
    echo ":==*** Fail with fsck ***"
    echo
    exit 1
}


# Function to assert the exit code of a command
assert_equal() {
    local a="$1"
    local b="$2"
    local msg="$3"

    # Check if the actual exit code matches the expected exit code
    if (( a != b)); then
        echo "assertion ($a == $b) failed: $msg"
        exit 1
    fi
}


# Expects the values to be not equal, asserts if equal
assert_ne() {
    local a="$1"
    local b="$2"
    local msg="$3"

    if (( a == b )); then
        echo "assertion ($a == $b) failed: $msg"
        exit 1
    fi
}

assert_lt() {
    local a="$1"
    local b="$2"
    local msg="$3"

    # assertion is (a < b)
    if (( a >= b)); then
        echo "assertion ($a == $b) failed: $msg"
        exit 1
    fi
}

assert_file_size () {
    file=$1
    size=$2
    MSG=$3

    actual_size=$(stat -c%s "$MPT/ddtest")

    # Compare the file size with the known size
    if [ "$actual_size" -ne "$size" ]; then
	fail "assert_file_size: expect/actual $size/$actual_size: $MSG"
    fi

}


verify_not_mounted () {
    local DEV=$1
    local MPT=$2
    local MSG=$3

    echo ":== verify_not_mounted: $DEV $MPT"

    # famfs case: succeed if DEV is mounted at MPT as famfs
    if findmnt -rn -t famfs -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        fail "verify_not_mounted: v1: $MSG"
    else
        # generic case: succeed if DEV is mounted at MPT (any type)
        if findmnt -rn -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
            fail "verify_not_mounted: fuse: $MSG"
        fi
    fi

    echo ":== verify_not_mounted: good on $DEV $MPT"
}


verify_mounted () {
    local DEV=$1
    local MPT=$2
    local MSG=$3

    echo ":== verify_mounted: $DEV $MPT"

    # Check if DEV is mounted at MPT as famfs (v1)
    if findmnt -rn -t famfs -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        echo ":== verify_mounted: famfs match on $DEV $MPT"
        echo ":== verify_mounted: good on $DEV $MPT"
        return 0
    fi

    # Fallback: check if DEV is mounted at MPT (any filesystem type)
    if findmnt -rn -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        echo ":== verify_mounted: generic match on $DEV $MPT"
        echo ":== verify_mounted: good on $DEV $MPT"
        return 0
    fi

    # If neither check matched, it's not mounted
    fail "verify_mounted: $MSG"
}

get_device_size () {
    DEV=$1
    BASENAME=$(basename $DEV)
    if [[ "$BASENAME" == *"pmem"* ]]; then
	raw_size=$("sudo cat /sys/class/block/$BASENAME/size")
    elif [[ "$BASENAME" == *"dax"* ]]; then
	majorno=$(stat -c %Ld $DEV)
	minorno=$(stat -c %Hd $DEV)
	raw_size=$("cat /sys/dev/char/$majorno:$minorno/size")
	raw_size=$((raw_size * 512))
    else
	echo "Failed to get device size for $DEV"
	exit -1
    fi
    echo "$DEV size: $raw_size"
}

get_famfs_abi_version() {
    # Define the path to the header file
    HEADER_FILE="/usr/include/linux/famfs_ioctl.h"

    # Check if the file exists
    if [[ ! -f "$HEADER_FILE" ]]; then
	echo "Error: File $HEADER_FILE not found."
	exit 1
    fi

    # Extract the value of the macro
    FAMFS_KABI_VERSION=$(grep -E '^#define FAMFS_KABI_VERSION' "$HEADER_FILE" | awk '{print $3}')

    # Check if the macro was found
    if [[ -z "$FAMFS_KABI_VERSION" ]]; then
	fail "Error: Macro FAMFS_KABI_VERSION not found in $HEADER_FILE."
    fi

    # Set the variable and output the result
    echo "$FAMFS_KABI_VERSION"
}

famfs_get_capacity() {
    MPT=$1
    ${CLI} fsck  $MPT | grep "Device capacity" | awk  '{ print $3 }'    
}

famfs_recreate() {
    cwd=$(pwd)

    MSG=$1

    sudo umount $MPT # || fail "famfs_recreate ($MSG)"
    verify_not_mounted $DEV $MPT "famfs_recreate ($MSG)"
    # Above this line should be the same for all smoke tests

    sudo mkdir -p $MPT || fail "famfs_recreate: mkdir -t MPT ($MSG)"

    # Make sure famfs is not mounted
    findmnt -t famfs $MPT
    if (( $? == 0 )); then
	sudo umount $MPT
    fi

    # destroy famfs file system, if any
    ${MKFS} /tmp/nonexistent && fail "famfs_recreate: mkfs on nonexistent dev should fail ($MSG)"
    ${MKFS} -f -k $DEV    || fail "famfs_recreate: mkfs/kill should succeed wtih --force ($MSG)"
    ${MKFS}  $DEV         || fail "famfs_recreate: mkfs ($MSG)"

    if [[ "$FAMFS_MODE" == "v1" ]]; then
	sudo modprobe ${FAMFS_MOD} || fail "famfs_recreate: modprobe ($MSG)"
    fi

    ${MOUNT} $DEV $MPT    || fail "famfs_recreate: famfs mount ($MSG)"

    verify_mounted "$DEV" "$MPT" "famfs_recreate ($MSG)"
}

famfs_fuse_supported() {
    grep -c fuse_file_famfs /proc/kallsyms
}

famfs_v1_supported() {
    $(modprobe --dry-run famfsv1)
    if [ "$?" -eq 0 ]; then
	echo 1
    else
	$(modprobe --dry-run famfs)
	if  [ "$?" -eq 0 ]; then
	    echo 1
	else
	    echo 0
	fi
    fi
}

generate_random_int() {
    local min=$1
    local max=$2

    if [[ -z "$min" || -z "$max" || "$min" -gt "$max" ]]; then
	echo "Usage: generate_random_int <min> <max>"
	return 1
    fi

    local range=$((max - min + 1))
    local rand=$((RANDOM % range + min))
    echo "$rand"
}

overwrite_page () {
    local file=$1
    local pgnum=$2

    dd if=/dev/urandom of=$file bs=4096 count=1 seek=$pgnum conv=notrunc
}
