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

CLI="sudo debug/tagfs"

set -x

# Try to create a file that is not in a tagfs file system (assume relative path not in one)
NOT_IN_TAGFS=no_leading_slash
${CLI} creat -s 0x400000 -f $NOT_IN_TAGFS \
     && fail "creating file not in tagfs file system should fail"

# Tagfs getmap should succeed on a file that exists
LOG=$MPT/.meta/.log
${CLI} getmap $LOG || fail "getmap should succeed on the tagfs log file"


# tagfs getmap should fail on a file that does not exist
NOTEXIST=$MPT/not_exist
${CLI} getmap $NOT_EXIST && fail "getmap should fail non nonexistent file in tagfs"

# tagfs getmap should fail on a file that is not in a tagfs file system
${CLI} getmap $NOT_IN_TAGFS && fail "getmap should fail if file not in tagfs"

F=bigtest2
${CLI} creat -r -S 42 -s 0x8000000 -f $MPT/$F   || fail "creat $F"
${CLI} verify -S 42 -f $MPT/$F                  || fail "$F mismatch"

${CLI} fsck $DEV || fail "fsck should not fail when nothing cloned"

${CLI} clone $MPT/${F} $MPT/${F}_clone        || fail "clone $F "
${CLI} clone $MPT/${F} $MPT/${F}_clone1        || fail "clone $F "

${CLI} fsck $DEV && fail "fsck should fail after cloning $F "
${CLI} verify -S 42 -f $MPT/${F}_clone || fail "${F}_clone mismatch"

sudo rm $MPT/${F}_clone || fail "should be able to rm $MPT/$F"

${CLI} fsck $DEV || fail "fsck should succeed after removing clone ${F}_clone"


set +x
echo "*************************************************************************************"
echo "Test2 completed successfully"
echo "*************************************************************************************"
