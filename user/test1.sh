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

sudo debug/tagfs creat -r -s 8192 -S 10 -f $MPT/test10   || fail "creat test1"
sudo debug/tagfs verify -S 10 -f $MPT/test10 || fail "verify test1 after replay"

set +x
echo "*************************************************************************************"
echo "Test1 completed successfully"
echo "*************************************************************************************"
