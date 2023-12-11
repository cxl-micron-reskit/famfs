#!/usr/bin/env bash

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
MPT=/mnt/famfs
MULTICHASE="sudo ../multichase/multichase"

CLI="sudo debug/famfs"

source test_funcs.sh
set -x

verify_mounted $DEV $MPT "test2.sh"


${CLI} creat -s 3g  ${MPT}/memfile
${MULTICHASE} -d ${MPT}/memfile -m 2900m || fail "multichase fail"

set +x
echo "*************************************************************************************"
echo "Test4 (multichase) completed successfully"
echo "*************************************************************************************"
