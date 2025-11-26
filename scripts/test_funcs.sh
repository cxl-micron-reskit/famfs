# This file is not for running, it is for sourcing into other scripts

# fail() unchanged
fail () {
    set +x
    echo
    echo ":==*** Fail ***"
    echo ":==$TEST: $1"
    echo
    exit 1
}

# Helper: detect whether errexit is currently set
_has_errexit() {
    case $- in
        *e*) return 0 ;;
        *)   return 1 ;;
    esac
}

# Expect the command to succeed (exit 0). Fail on non-zero exits and on crashes (signals).
# Usage:
#   expect_good <command> [args...]
#   expect_good <command> [args...] -- <failure message...>
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

    [[ -z $msg ]] && msg="expected success"

    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to expect_good"
    fi

    echo ":== expect_good: ${cmd[@]}"

    # Save and disable errexit
    local had_errexit=0
    if _has_errexit; then
        had_errexit=1
        set +e
    fi

    # Run the command with errexit disabled
    "${cmd[@]}"
    local status=$?

    # IMPORTANT: classify status while errexit is still disabled
    if (( status == 0 )); then
        echo ":== expect_good: good completion"
        # Restore errexit state
        (( had_errexit )) && set -e
        return 0
    elif (( status >= 128 )); then
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        (( had_errexit )) && set -e
        fail "expect_good: ${msg}: bad failure (terminated by ${signame} [$signo], exit=$status)"
    else
        (( had_errexit )) && set -e
        fail "expect_good: ${msg}: unexpected exit code $status"
    fi
}

# Run a command that is expected to fail; flag crashes as bad failures.
# Usage:
#   expect_fail <command> [args...]
#   expect_fail <command> [args...] -- <failure message...>
expect_fail () {
    local -a cmd=()
    local msg=""
    local saw_delim=0

    # Parse args
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

    [[ -z $msg ]] && msg="expected failure"

    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to expect_fail"
    fi

    echo ":== expect_fail: ${cmd[@]}"

    # Save and disable errexit
    local had_errexit=0
    if _has_errexit; then
        had_errexit=1
        set +e
    fi

    # Run the command with errexit disabled
    "${cmd[@]}"
    local status=$?

    # Classify while errexit still disabled
    if (( status == 0 )); then
        (( had_errexit )) && set -e
        fail "${msg}: command succeeded unexpectedly"
    elif (( status >= 128 )); then
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        (( had_errexit )) && set -e
        fail "${msg}: bad failure (terminated by ${signame} [$signo], exit=$status)"
    else
        echo ":== expect_fail: good failure (${msg}, exit $status)"
        (( had_errexit )) && set -e
        return 0
    fi
}

# Run a command that is expected to fail; flag crashes and selected exit codes as bad failures.
# Usage:
#   expect_fail_except [--bad CODE|-b CODE]... <command> [args...] [-- <failure message...>]
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
                if [[ -z "${1:-}" ]]; then
                    fail "expected a numeric exit code after --bad/-b"
                fi
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

    if (( $# > 0 )); then
        msg="$*"
    fi
    [[ -z $msg ]] && msg="expected failure"

    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to expect_fail_except"
    fi

    echo ":== expect_fail_except(${bad_codes[@]}): ${cmd[@]}"

    # Save and disable errexit
    local had_errexit=0
    if _has_errexit; then
        had_errexit=1
        set +e
    fi

    # Run with errexit disabled
    "${cmd[@]}"
    local status=$?

    # Check membership in bad_codes
    local is_bad_code=0
    for bc in "${bad_codes[@]}"; do
        if (( status == bc )); then
            is_bad_code=1
            break
        fi
    done

    # Classify while errexit still disabled
    if (( status == 0 )); then
        (( had_errexit )) && set -e
        fail "${msg}: command succeeded unexpectedly"
    elif (( status >= 128 )); then
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        (( had_errexit )) && set -e
        fail "${msg}: bad failure (terminated by ${signame} [$signo], exit=$status)"
    elif (( is_bad_code )); then
        (( had_errexit )) && set -e
        fail "${msg}: bad failure (unacceptable exit code $status)"
    else
        echo ":== expect_fail_except(${bad_codes[@]}): good failure (${msg}, exit $status)"
        (( had_errexit )) && set -e
        return 0
    fi
}

# Run a command and only stop (call fail) if it crashes due to a signal.
# Usage:
#   stop_on_crash <command> [args...]
#   stop_on_crash <command> [args...] -- <failure message...>
stop_on_crash () {
    local -a cmd=()
    local msg=""
    local saw_delim=0

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

    [[ -z $msg ]] && msg="bad failure"

    if [[ ${#cmd[@]} -eq 0 ]]; then
        fail "${msg}: no command provided to stop_on_crash"
    fi

    echo ":== stop_on_crash: ${cmd[@]}"

    # Save and disable errexit
    local had_errexit=0
    if _has_errexit; then
        had_errexit=1
        set +e
    fi

    # Run with errexit disabled
    "${cmd[@]}"
    local status=$?

    # Classify while errexit still disabled
    if (( status >= 128 )); then
        local signo=$(( status - 128 ))
        local signame
        signame=$(kill -l "$signo" 2>/dev/null || echo "SIG$signo")
        (( had_errexit )) && set -e
        fail "${msg}: terminated by ${signame} [$signo], exit=$status"
    fi

    echo ":== stop_on_crash: command did not crash: ${cmd[@]}"
    (( had_errexit )) && set -e
    return 0
}

fail_fsck () {
    set +x
    echo
    echo ":==*** Fail with fsck ***"
    echo ":==$TEST: $1"
    echo
    # $2, if provided, is fsck args (e.g. "-B 21")
    local extra_args=()
    [[ -n "${2:-}" ]] && extra_args+=("$2")
    "${CLI[@]}" fsck -v "${extra_args[@]}" "$MPT"
    echo ":==$TEST: $1"
    echo ":==*** Fail with fsck ***"
    echo
    exit 1
}

assert_equal() {
    local a="$1"
    local b="$2"
    local msg="$3"

    if (( a != b )); then
        echo "assertion ($a == $b) failed: $msg"
        exit 1
    fi
}

assert_ne() {
    local a="$1"
    local b="$2"
    local msg="$3"

    if (( a == b )); then
        echo "assertion ($a != $b) failed: $msg"
        exit 1
    fi
}

assert_lt() {
    local a="$1"
    local b="$2"
    local msg="$3"

    if (( a >= b )); then
        echo "assertion ($a < $b) failed: $msg"
        exit 1
    fi
}

assert_file_size () {
    local file=$1
    local size=$2
    local MSG=$3

    local actual_size
    actual_size=$(stat -c%s "$file")

    if [[ "$actual_size" -ne "$size" ]]; then
        fail "assert_file_size: expect/actual $size/$actual_size: $MSG"
    fi
}

verify_not_mounted () {
    local DEV=$1
    local MPT=$2
    local MSG=$3

    echo ":== verify_not_mounted: $DEV $MPT"

    if findmnt -rn -t famfs -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        fail "verify_not_mounted: v1: $MSG"
    else
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

    if findmnt -rn -t famfs -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        echo ":== verify_mounted: famfs match on $DEV $MPT"
        echo ":== verify_mounted: good on $DEV $MPT"
        return 0
    fi

    if findmnt -rn -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        echo ":== verify_mounted: generic match on $DEV $MPT"
        echo ":== verify_mounted: good on $DEV $MPT"
        return 0
    fi

    fail "verify_mounted: $MSG"
}

get_device_size () {
    local DEV=$1
    local BASENAME
    BASENAME=$(basename "$DEV")
    local raw_size

    if [[ "$BASENAME" == *"pmem"* ]]; then
        raw_size=$(sudo cat "/sys/class/block/$BASENAME/size")
        # size is already in 512-byte sectors for block devices; caller can scale if needed
    elif [[ "$BASENAME" == *"dax"* ]]; then
        local majorno minorno
        majorno=$(stat -c %L "$DEV")
        minorno=$(stat -c %H "$DEV")
        raw_size=$(cat "/sys/dev/char/$majorno:$minorno/size")
        raw_size=$((raw_size * 512))
    else
        echo "Failed to get device size for $DEV"
        exit 1
    fi
    echo "$DEV size: $raw_size"
}

get_famfs_abi_version() {
    local HEADER_FILE="/usr/include/linux/famfs_ioctl.h"

    if [[ ! -f "$HEADER_FILE" ]]; then
        echo "Error: File $HEADER_FILE not found."
        exit 1
    fi

    local FAMFS_KABI_VERSION
    FAMFS_KABI_VERSION=$(grep -E '^#define FAMFS_KABI_VERSION' "$HEADER_FILE" | awk '{print $3}')

    if [[ -z "$FAMFS_KABI_VERSION" ]]; then
        fail "Error: Macro FAMFS_KABI_VERSION not found in $HEADER_FILE."
    fi

    echo "$FAMFS_KABI_VERSION"
}

famfs_get_capacity() {
    local MPT=$1
    "${CLI[@]}" fsck "$MPT" | grep "Device capacity" | awk '{ print $3 }'
}

famfs_recreate() {
    local MSG=$1

    # Ensure not mounted
    sudo umount "$MPT" >/dev/null 2>&1 || true
    verify_not_mounted "$DEV" "$MPT" "famfs_recreate ($MSG)"

    sudo mkdir -p "$MPT" || fail "famfs_recreate: mkdir -p MPT ($MSG)"

    # Make sure famfs is not mounted
    if findmnt -t famfs "$MPT" >/dev/null 2>&1; then
        sudo umount "$MPT"
    fi

    # destroy famfs file system, if any
    "${MKFS[@]}" /tmp/nonexistent && fail "famfs_recreate: mkfs on nonexistent dev should fail ($MSG)"
    "${MKFS[@]}" -f -k "$DEV"      || fail "famfs_recreate: mkfs/kill should succeed with --force ($MSG)"
    "${MKFS[@]}" "$DEV"            || fail "famfs_recreate: mkfs ($MSG)"

    if [[ "$FAMFS_MODE" == "v1" ]]; then
        sudo modprobe "${FAMFS_MOD}" || fail "famfs_recreate: modprobe ($MSG)"
    fi

    "${MOUNT[@]}" "$DEV" "$MPT"    || fail "famfs_recreate: famfs mount ($MSG)"

    verify_mounted "$DEV" "$MPT" "famfs_recreate ($MSG)"
}

famfs_fuse_supported() {
    grep -c fuse_file_famfs /proc/kallsyms
}

famfs_v1_supported() {
    if modprobe --dry-run famfsv1 >/dev/null 2>&1; then
        echo 1
    elif modprobe --dry-run famfs >/dev/null 2>&1; then
        echo 1
    else
        echo 0
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

    dd if=/dev/urandom of="$file" bs=4096 count=1 seek="$pgnum" conv=notrunc
}

start_test () {
    local TEST=$1
    echo ":==----------------------------------------------------------------"
    echo ":==$TEST started"
    echo ":==----------------------------------------------------------------"
}

finish_test () {
    local TEST=$1
    echo ":==*****************************************************************"
    echo ":==$TEST completed successfully"
    echo ":==*****************************************************************"
}
