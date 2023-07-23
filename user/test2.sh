#/usr/bin/bash

fail () {
    set +x
    echo
    echo "*** Fail ***"
    echo "$1"
    echo
    exit 1
}

cwd=$(pwd)
export PATH=cwd/debug:$PATH

DEV=/dev/pmem0
MPT=/mnt/tagfs

set -x

# Try to create a file that is not in a tagfs file system (assume relative path not in one)
NOT_IN_TAGFS=no_leading_slash
sudo debug/tagfs creat -n 2 -s 0x400000 -o 0 -l 0x200000 -o 0x200000 -l 0x200000 -f $NOT_IN_TAGFS \
     && fail "creating file not in tagfs file system should fail"


# Try to create a file that is cross linked with the superblock (offset 0)
F=$MPT/cross-linked-file
sudo debug/tagfs creat -n 2 -s 0x400000 -o 0 -l 0x200000 -o 0x200000 -l 0x200000 -f $F \
     && fail "creating file cross-linked with superblock should fail"

# Tagfs getmap should succeed on a file that exists
LOG=$MPT/.meta/.log
sudo debug/tagfs getmap $LOG || fail "getmap should succeed on the tagfs log file"


# tagfs getmap should fail on a file that does not exist
NOTEXIST=$MPT/not_exist
sudo debug/tagfs getmap $NOT_EXIST && fail "getmap should fail non nonexistent file in tagfs"

# tagfs getmap should fail on a file that is not in a tagfs file system
sudo debug/tagfs getmap $NOT_IN_TAGFS && fail "getmap should fail if file not in tagfs"

set +x
echo "*************************************************************************************"
echo "Test2 completed successfully"
echo "*************************************************************************************"
