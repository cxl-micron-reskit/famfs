
# This file is not for running, it is for sourcing into other scripts

fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

mount_only () {
    DEV=$1
    MPT=$2
    sudo mount -t tagfs -o noatime -o dax=always -o rootdev=$DEV $DEV $MPT
    #echo $?
    return $?
}

mkmeta_only () {
    DEV=$1
    ${CLI} mkmeta $DEV
    return $?
}

#
# Getting into the "fully mounted" state requires mount, mkmeta, logplay
#
full_mount () {
    DEV=$1
    MPT=$2
    MSG=$3
    sudo mount -t tagfs -o noatime -o dax=always -o rootdev=$DEV $DEV $MPT \
	                              || fail "full_mount: mount err: $MSG"
    ${CLI} mkmeta $DEV                  || fail "full_mount: mkmeta err: $MSG"
    ${CLI} logplay $MPT               || fail "full_mount: logplay err: $MSG"

}

verify_not_mounted () {
    DEV=$1
    MPT=$2
    MSG=$3
    grep -c $DEV /proc/mounts && fail "verify_not_mounted: $DEV in /proc/mounts ($MSG)"
    grep -c $MPT /proc/mounts && fail "verify_not_mounted: $MPT in /proc/mounts ($MSG)"
}

verify_mounted () {
    DEV=$1
    MPT=$2
    MSG=$3
    grep -c $DEV /proc/mounts || fail "verify_mounted: $DEV not in /proc/mounts ($MSG)"
    grep -c $MPT /proc/mounts || fail "verify_mounted: $MPT not in /proc/mounts ($MSG)"
}

