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

# Global variable:
TRACE_MARKER_AVAILABLE=N

TRACE_MARKER_AVAILABLE=N

init_trace_marker_available() {
    # Existence test must be done with sudo because non-root can't see debugfs.
    if sudo test -e /sys/kernel/debug/tracing/trace_marker 2>/dev/null; then
        # Now test writability (requires root-capable sudo as well)
        if sudo test -w /sys/kernel/debug/tracing/trace_marker 2>/dev/null; then
            TRACE_MARKER_AVAILABLE=Y
        else
            TRACE_MARKER_AVAILABLE=N
        fi
    else
        TRACE_MARKER_AVAILABLE=N
    fi
}

init_trace_marker_available
echo "TRACE_MARKER_AVAILABLE: $TRACE_MARKER_AVAILABLE"

emit_to_logs() {
    if [[ "$LOG_CMDS" != "Y" ]]; then
        return
    fi

    # Recreate the command line from array args
    local -a argv=("$@")
    local cmd
    cmd=$(printf '%q ' "${argv[@]}")
    cmd="${cmd%" "}"     # trim trailing space

    local msg="$HARNESS: $TEST: $cmd"

    # famfs log
    logger -t famfs "$msg"

    # kernel facility (optional: harmless if ignored by journald)
    echo "<5>$msg" | sudo tee /dev/kmsg > /dev/null

    # trace marker if enabled
    if [[ "$TRACE_MARKER_AVAILABLE" == "Y" ]]; then
        # use sudo for the write
	local trc_marker=/sys/kernel/debug/tracing/trace_marker
        echo "<5>$msg" | sudo tee $trc_marker > /dev/null
    fi

    sleep 0.05
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

    echo ":= expect_good: ${cmd[@]}"

    # Save and disable errexit
    local had_errexit=0
    if _has_errexit; then
        had_errexit=1
        set +e
    fi

    emit_to_logs "${cmd[@]}"

    # Run the command with errexit disabled
    "${cmd[@]}"
    local status=$?

    # IMPORTANT: classify status while errexit is still disabled
    if (( status == 0 )); then
        echo ":= expect_good: good completion"
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

    echo ":= expect_fail: ${cmd[@]}"

    # Save and disable errexit
    local had_errexit=0
    if _has_errexit; then
        had_errexit=1
        set +e
    fi

    emit_to_logs "${cmd[@]}"

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
        echo ":= expect_fail: good failure (${msg}, exit $status)"
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

    emit_to_logs "${cmd[@]}"

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
        echo ":= expect_fail_except(${bad_codes[@]}): good failure (${msg}, exit $status)"
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

    echo ":= stop_on_crash: ${cmd[@]}"

    # Save and disable errexit
    local had_errexit=0
    if _has_errexit; then
        had_errexit=1
        set +e
    fi

    emit_to_logs "${cmd[@]}"

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

    echo ":= stop_on_crash: command did not crash: ${cmd[@]}"
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

    echo ":= verify_not_mounted: $DEV $MPT"

    if findmnt -rn -t famfs -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        fail "verify_not_mounted: v1: $MSG"
    else
        if findmnt -rn -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
            fail "verify_not_mounted: fuse: $MSG"
        fi
    fi

    echo ":= verify_not_mounted: good on $DEV $MPT"
}

verify_dev_not_mounted () {
    local DEV=$1
    local MSG=$2
    local mounted_at

    echo ":= verify_dev_not_mounted: $DEV"

    mounted_at=$(findmnt -rn -S "$DEV" -o TARGET 2>/dev/null || true)
    if [[ -n "$mounted_at" ]]; then
        fail "verify_dev_not_mounted: $DEV is mounted at $mounted_at: $MSG"
    fi

    echo ":= verify_dev_not_mounted: good on $DEV"
}

# famfs_mount_alive <mountpoint>
#
# Returns:
#   0 => Alive
#   1 => Not mounted
#   2 => FUSE mount found but famfs_fused not responding
mount_alive() {
    local mpt="$1"
    local entry opts curl_rc

    # Lookup the mount entry for this mountpoint
    entry=$(awk -v mpt="$mpt" '$2 == mpt {print}' /proc/mounts)

    if [[ -z "$entry" ]]; then
        echo ":= mount_alive: nothing mounted at $mpt"
        return 1
    fi

    # Extract mount options (4th column)
    opts=$(echo "$entry" | awk '{print $4}')

    # FUSE?
    if [[ "$opts" == *shadow=* ]]; then
        # curl must not trigger set -e if it fails
        set +e
        sudo curl \
            --unix-socket "$(scripts/famfs_shadow.sh "$mpt")/sock" \
            http://localhost/pid >/dev/null 2>&1
        curl_rc=$?
        set -e

        if (( curl_rc != 0 )); then
            echo ":== mount_alive: Error: famfs_fused not responding on $mpt (curl rc=$curl_rc)"
            return 2
        fi

        echo ":= mount_alive: fuse mount alive on $mpt"
        return 0
    fi

    echo ":= mount_alive: non-fuse mount alive"
    return 0
}

verify_mounted () {
    local DEV=$1
    local MPT=$2
    local MSG=$3

    echo ":= verify_mounted: $DEV $MPT"

    if findmnt -rn -t famfs -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        echo ":= verify_mounted: famfs match on $DEV $MPT"
        echo ":= verify_mounted: good on $DEV $MPT"
        mount_alive "$MPT" || fail "mount not alive: $MSG"
        return 0
    fi

    if findmnt -rn -S "$DEV" --target "$MPT" >/dev/null 2>&1; then
        echo ":= verify_mounted: generic match on $DEV $MPT"
        echo ":= verify_mounted: good on $DEV $MPT"
        mount_alive "$MPT" || fail "mount not alive: $MSG"
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

# show_dax_config <daxdev>
#
# Display alignment and mode for a dax device.
# Accepts either /dev/daxX.Y or daxX.Y format.
# Calls fail() if device not found or alignment mismatch between sysfs and daxctl.
#
# Example:
#   show_dax_config /dev/dax1.0
#   show_dax_config dax2.0
show_dax_config () {
    local DEV_INPUT="$1"
    local DEV
    local SYSFS_ALIGN
    local DAXCTL_ALIGN
    local MODE
    local SIZE
    local STATE

    if [ -z "$DEV_INPUT" ]; then
        echo "Usage: show_dax_config <daxdev>"
        return 1
    fi

    # Strip /dev/ prefix if present
    DEV=$(basename "$DEV_INPUT")

    # Verify device exists
    if [ ! -e "/sys/bus/dax/devices/${DEV}" ]; then
        fail "bad daxdev ${DEV}"
    fi

    # Read alignment from sysfs
    SYSFS_ALIGN=$(cat "/sys/bus/dax/devices/${DEV}/align" 2>/dev/null)
    SIZE=$(cat "/sys/bus/dax/devices/${DEV}/size" 2>/dev/null)

    # Get alignment, mode and state from daxctl
    DAXCTL_ALIGN=$(daxctl list 2>/dev/null | grep -A10 "\"chardev\":\"${DEV}\"" | grep "\"align\":" | head -1 | sed 's/.*"align":\([0-9]*\).*/\1/')
    MODE=$(daxctl list 2>/dev/null | grep -A10 "\"chardev\":\"${DEV}\"" | grep "\"mode\":" | head -1 | cut -d'"' -f4)
    STATE=$(daxctl list 2>/dev/null | grep -A10 "\"chardev\":\"${DEV}\"" | grep "\"state\":" | head -1 | cut -d'"' -f4)

    # If no state field returned, device is enabled
    [ -z "$STATE" ] && STATE="enabled"

    # Verify alignment agreement between sysfs and daxctl
    if [ -n "$SYSFS_ALIGN" ] && [ -n "$DAXCTL_ALIGN" ]; then
        if [ "$SYSFS_ALIGN" != "$DAXCTL_ALIGN" ]; then
            fail "dax ${DEV} alignment mismatch: sysfs=${SYSFS_ALIGN} daxctl=${DAXCTL_ALIGN}"
        fi
    fi

    # Convert alignment to human-readable and verify it's a valid value
    local ALIGN_HR
    case "$SYSFS_ALIGN" in
        4096)       ALIGN_HR="4KiB" ;;
        2097152)    ALIGN_HR="2MiB" ;;
        1073741824) ALIGN_HR="1GiB" ;;
        *)          fail "dax ${DEV} invalid alignment: ${SYSFS_ALIGN}" ;;
    esac

    # Convert size to human-readable
    local SIZE_HR
    if [ -n "$SIZE" ]; then
        if [ "$SIZE" -ge 1073741824 ]; then
            SIZE_HR="$((SIZE / 1073741824)) GiB"
        elif [ "$SIZE" -ge 1048576 ]; then
            SIZE_HR="$((SIZE / 1048576)) MiB"
        else
            SIZE_HR="${SIZE} bytes"
        fi
    else
        SIZE_HR="unknown"
    fi

    echo ":== DAX device: /dev/${DEV}"
    echo ":==   Alignment: ${ALIGN_HR} (${SYSFS_ALIGN})"
    echo ":==   Size:      ${SIZE_HR}"
    echo ":==   Mode:      ${MODE:-unknown}"
    echo ":==   State:     ${STATE}"

    emit_to_logs "${DEV} Alignment: ${ALIGN_HR}"
    emit_to_logs "${DEV} Mode:      ${MODE:-unknown}"
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
