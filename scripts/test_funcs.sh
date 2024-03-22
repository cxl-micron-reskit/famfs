
# This file is not for running, it is for sourcing into other scripts

#
# Set to "char" if you convert the /dev/pmem device to a char dax device
# (e.g. /dev/dax0.0). As of this typing, this is not working yet for complex
# reasons)
#


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

mount_only () {
    DEV=$1
    MPT=$2
    sudo mount $MOUNT_OPTS $DEV $MPT
    return $?
}

mkmeta_only () {
    DEV=$1
    MSG=$2
    ${CLI} mkmeta $DEV || fail "mkmeta_only: $MSG"
}

#
# Getting into the "fully mounted" state requires mount, mkmeta, logplay
#
full_mount () {
    DEV=$1
    MPT=$2
    MO=$3
    MSG=$4
    sudo mount $MO $DEV $MPT  || fail "full_mount: mount err: $MSG"
    ${CLI} mkmeta $DEV                || fail "full_mount: mkmeta err: $MSG"
    ${CLI} logplay $MPT               || fail "full_mount: logplay err: $MSG"

}

verify_not_mounted () {
    DEV=$1
    MPT=$2
    MSG=$3
    findmnt -t famfs $MPT && fail "verify_not_mounted: famfs still mounted at $MPT"
}

verify_mounted () {
    DEV=$1
    MPT=$2
    MSG=$3
    findmnt -t famfs $MPT || fail "verify_mounted: famfs not mounted at $MPT"
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
