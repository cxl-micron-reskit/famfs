#!/usr/bin/bash

fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

if [ -z "$UMOUNT" ]; then
    UMOUNT="umount"
fi

# Unmount all mounted instances of famfs
mount | grep famfs | awk '{print $3}' | while IFS= read -r path; do
    if [ -d $path ]; then
	echo "Unmounting $path"
	sudo umount $path
    fi
done

sudo rmmod famfs
