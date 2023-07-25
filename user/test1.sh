#/usr/bin/bash

cwd=$(pwd)
export PATH=cwd/debug:$PATH

DEV=/dev/pmem0
MPT=/mnt/tagfs

CLI="sudo debug/tagfs"

source test_funcs.sh

set -x

verify_mounted $DEV $MPT "test1.sh"
sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"

${CLI} fsck $MPT && fail "fsck by path should fail when not mounted"
${CLI} fsck $DEV || fail "fsck by dev should succeed when not mounted"

full_mount $DEV $MPT "test1.sh"

${CLI} fsck $MPT || fail "fsck by path should succeed when mounted"

# Try exclusive open in driver to make this test good;
# (currently succeeds but it should fail)
${CLI} fsck $DEV && fail "fsck by dev should fail when mounted"

verify_mounted $DEV $MPT "test1.sh"

#
# Do stuff with files bigger than a page, cautiously
#

F=test10
${CLI} creat -r -s 8192 -S 10 -f $MPT/$F   || fail "creat $F"
${CLI} verify -S 10 -f $MPT/$F || fail "verify $F after replay"

F=bigtest0
${CLI} creat -r -S 42 -s 0x800000 -f $MPT/$F   || fail "creat $F"
${CLI} verify -S 42 -f $MPT/$F                 || fail "$F mismatch"

${CLI} cp $MPT/$F $MPT/${F}_cp      || fail "cp $F"
${CLI} verify -S 42 -f $MPT/${F}_cp || fail "verify ${F}_cp"

${CLI} cp $MPT/$F $MPT/${F}_cp0      || fail "cp0 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp1      || fail "cp1 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp2      || fail "cp2 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp3      || fail "cp3 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp4      || fail "cp4 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp5      || fail "cp5 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp6      || fail "cp6 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp7      || fail "cp7 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp8      || fail "cp8 $F"
${CLI} cp $MPT/$F $MPT/${F}_cp9      || fail "cp9 $F"

${CLI} verify -S 42 -f $MPT/${F}_cp0 || fail "verify ${F}_cp0"
${CLI} verify -S 42 -f $MPT/${F}_cp1 || fail "verify ${F}_cp1"
${CLI} verify -S 42 -f $MPT/${F}_cp2 || fail "verify ${F}_cp2"
${CLI} verify -S 42 -f $MPT/${F}_cp3 || fail "verify ${F}_cp3"
${CLI} verify -S 42 -f $MPT/${F}_cp4 || fail "verify ${F}_cp4"
${CLI} verify -S 42 -f $MPT/${F}_cp5 || fail "verify ${F}_cp5"
${CLI} verify -S 42 -f $MPT/${F}_cp6 || fail "verify ${F}_cp6"
${CLI} verify -S 42 -f $MPT/${F}_cp7 || fail "verify ${F}_cp7"
${CLI} verify -S 42 -f $MPT/${F}_cp8 || fail "verify ${F}_cp8"
${CLI} verify -S 42 -f $MPT/${F}_cp9 || fail "verify ${F}_cp9"

sudo umount $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "test1.sh"
verify_mounted $DEV $MPT "test1.sh"

${CLI} verify -S 42 -f $MPT/${F}_cp || fail "verify ${F}_cp"

${CLI} verify -S 42 -f $MPT/${F}_cp0 || fail "verify ${F}_cp0"
${CLI} verify -S 42 -f $MPT/${F}_cp1 || fail "verify ${F}_cp1"
${CLI} verify -S 42 -f $MPT/${F}_cp2 || fail "verify ${F}_cp2"
${CLI} verify -S 42 -f $MPT/${F}_cp3 || fail "verify ${F}_cp3"
${CLI} verify -S 42 -f $MPT/${F}_cp4 || fail "verify ${F}_cp4"
${CLI} verify -S 42 -f $MPT/${F}_cp5 || fail "verify ${F}_cp5"
${CLI} verify -S 42 -f $MPT/${F}_cp6 || fail "verify ${F}_cp6"
${CLI} verify -S 42 -f $MPT/${F}_cp7 || fail "verify ${F}_cp7"
${CLI} verify -S 42 -f $MPT/${F}_cp8 || fail "verify ${F}_cp8"
${CLI} verify -S 42 -f $MPT/${F}_cp9 || fail "verify ${F}_cp9"

${CLI} fsck $MPT || fail "fsck should succeed"

set +x
echo "*************************************************************************************"
echo "Test1 completed successfully"
echo "*************************************************************************************"
