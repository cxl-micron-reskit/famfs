#!/usr/bin/env bash

# This scripts attampt to validate device DEV as a home for a famfs file system
# 
if (($# > 0)); then
    DEV="$1"
fi

# Allow these variables to be set from the environment
if [ -z "$DEV" ]; then
    echo "Must specify DEV"
    exit -1
fi

errs=0
basename=$(basename $DEV)
path=$(dirname $DEV)

if command -v ndctl >/dev/null 2>&1; then
    echo "ndctl is installed"
else
    echo "Error: ndctl is not installed"
    (( errs++ ))
fi
if command -v daxctl >/dev/null 2>&1; then
    echo "daxctl is installed"
else
    echo "Error: daxctl is not installed"
    (( errs++ ))
fi

if (( errs > 0 )); then
    echo "Error: Cannot check device configuration without ndctl and daxctl; please install them"
    exit -1
fi

if [ ! -e $DEV ]; then
    echo "Error: $DEV not found"
    exit -1
fi

if [ -b $DEV ]; then
    echo "$DEV: block device"
    exit 0
fi

if [ -c $DEV ]; then
    echo "$DEV: character device"
    modeline=$(daxctl list -d $basename | grep mode)

    if [[ "$modeline" == *"devdax"* ]]; then
	echo "$DEV is in devdax mode"
	exit 0;
    elif [[ "$modeline" == *"famfs"* ]]; then
	echo "$DEV is in famfs mode"
	exit 0;
    elif  [[ "$modeline" == *"system-ram"* ]]; then
	echo "Error: $DEV is in system-ram mode: you need to reconfigure it"
	echo "Try: sudo daxctl reconfigure-device --human --mode=devdax --force $DEV"
	exit -1
    else
	echo "Error: $DEV mode not recognized: $modeline"
	exit -1
    fi
fi

echo "Error: $DEV device not recognized"
