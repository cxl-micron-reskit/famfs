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

#
# Do stuff with files bigger than a page, cautiously
#

F=test10
${CLI} creat -r -s 8192 -S 10 -f $MPT/$F   || fail "creat $F"
${CLI} verify -S 10 -f $MPT/$F || fail "verify $F after replay"

F=bigtest0
${CLI} creat -r -S 42 -s 0x800000 -f $MPT/$F   || fail "creat $F"
# ${CLI} clone $MPT/bigtest0 $MPT/bigtest0_clone        || fail "clone bigtest0"
${CLI} verify -S 42 -f $MPT/$F                 || fail "$F mismatch"
#${CLI} verify -S 42 $MPT/bigtest0_clone || fail "bigtest0_clone mismatch"

${CLI} cp $MPT/$F $MPT/${F}_cp      || fail "cp $F"
${CLI} verify -S 42 -f $MPT/${F}_cp || fail "verify ${F}_cp"


set +x
echo "*************************************************************************************"
echo "Test1 completed successfully"
echo "*************************************************************************************"
