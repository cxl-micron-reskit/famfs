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

CLI="sudo debug/famfs"

source test_funcs.sh
set -x

verify_mounted $DEV $MPT "test2.sh"

sudo cmp $MPT/bigtest0 $MPT/bigtest0_cp       || fail "copies should match"
sudo cmp $MPT/bigtest10 $MPT/bigtest11        && fail "files should not match"

${CLI} creat -r -s 4096 -S 1 $MPT/ddtest   || fail "creat ddfile"
${CLI} verify -S 1 -f $MPT/test1              || fail "verify ddfile creat"
#sudo dd if=/dev/zero of=$MPT/ddtest bs=4096 conv=notrunc  || fail "dd into ddfile"
#${CLI} verify -S 1 -f $MPT/test1              && fail "verify should fail after dd overwrite"
sudo dd of=/dev/null if=$MPT/ddtest bs=4096   || fail "dd out of ddfile"



sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

# TODO: make logplay drop files that have allocation collisions
#test -f $MPT/bigtest0_clone && fail "cloned file should disappear after remount"

${CLI} fsck $MPT || fail "fsck should succeed - no cross links yet"


set +x
echo "*************************************************************************************"
echo "Test3 completed successfully"
echo "*************************************************************************************"
