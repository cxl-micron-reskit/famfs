
# This file is not for running, it is for sourcing into other scripts

fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$TEST: $1"
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
    DEV=$1
    MPT=$2
    MSG=$3

    count=$(findmnt -t famfs $MPT | grep -c $DEV)
    if (( count > 0 )); then
	fail "verify_not_mounted: v1: $MSG"
    else
	count=$(findmnt $MPT | grep -c $DEV)
	if (( count > 0 )); then
	    fail "verify_not_mounted: fuse: $MSG"
	fi
    fi
}

verify_mounted () {
    DEV=$1
    MPT=$2
    MSG=$3

    count=$(findmnt -t famfs $MPT | grep -c $DEV) # find v1 mounts
    if (( count == 0 )); then
	count=$(findmnt $MPT | grep -c $DEV)
	if (( count == 0 )); then
	    fail "verify_mounted: $MSG"
	fi
    fi
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
	sudo modprobe famfs || fail "famfs_recreate: modprobe ($MSG)"
    fi

    ${MOUNT} $DEV $MPT    || fail "famfs_recreate: famfs mount ($MSG)"

    verify_mounted "$DEV" "$MPT" "famfs_recreate ($MSG)"
}
