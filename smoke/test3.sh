#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
DEV="/dev/pmem0"
VG=""
SCRIPTS=../scripts
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
BIN=../debug
VALGRIND_ARG="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"

if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$UMOUNT" ]; then
    UMOUNT="umount"
fi

# Override defaults as needed
while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
	(-d|--device)
	    DEV=$1
	    shift;
	    ;;
	(-b|--bin)
	    BIN=$1
	    shift
	    ;;
	(-s|--scripts)
	    SCRIPTS=$1
	    source_root=$1;
	    shift;
	    ;;
	(-v|--valgrind)
	    # no argument to -v; just setup for Valgrind
	    VG=${VALGRIND_ARG}
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;

    esac
done

echo "DEVTYPE=$DEVTYPE"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
TEST="test3"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

verify_mounted $DEV $MPT "test2.sh"

sudo cmp $MPT/bigtest0 $MPT/bigtest0_cp       || fail "copies should match"
sudo cmp $MPT/bigtest10 $MPT/bigtest11        && fail "files should not match"

${CLI} creat -r -s 4096 -S 1 $MPT/ddtest    || fail "creat ddfile"
${CLI} verify -S 1 -f $MPT/test1            || fail "verify ddfile creat"
sudo dd if=/dev/zero of=$MPT/ddtest bs=4096 count=1 conv=notrunc  || fail "dd into ddfile"
${CLI} verify -S 1 -f $MPT/ddtest           && fail "verify should fail after dd overwrite"
sudo dd of=/dev/null if=$MPT/ddtest bs=4096 || fail "dd out of ddfile"

#
# Test some cases where the kmod should throw errors because the famfs file is
# not in a valid state
#
sudo truncate $MPT/ddtest -s 2048           || fail "truncate is hinky but should succeed"
sudo dd of=/dev/null if=$MPT/ddtest bs=2048 && fail "Any read from a truncated file should fail"
sudo truncate $MPT/ddtest -s 4096           || fail "truncate extra-hinky - back to original size"
sudo dd of=/dev/null if=$MPT/ddtest bs=2048 && fail "Read from previously horked file should fail"

# Test behavior of standard "cp" into famfs
# The create should succeed, but the write should fail, leaving an empty, invalid file
sudo cp /etc/passwd $MPT/pwd && fail "cp to famfs should fail due to invalid famfs metadata"
test -f $MPT/pwd || fail "cp should leave an invalid destination file"
test -s $MPT/pwd && fail "file from cp should be empty"

# Create an invalid file via "touch" and test behavior
sudo touch $MPT/touchfile || fail "touch should succeed at creating an invalid file"
sudo dd if=$MPT/touchfile && fail "dd from invalid file should fail"
sudo truncate $MPT/touchfile -s 8192 || fail "truncate failed"
sudo dd if=$MPT/touchfile of=/dev/null bs=8192 count=1  && fail "dd from touchfile should fail"
sudo dd if=/dev/zero of=$MPT/touchfile bs=8192 count=1  && fail "dd to touchfile should fail"

# unmount and remount
sudo $UMOUNT $MPT || fail "umount"
verify_not_mounted $DEV $MPT "test1.sh"
full_mount $DEV $MPT "${MOUNT_OPTS}"  "full_mount"
verify_mounted $DEV $MPT "test1.sh"

# Test that our invalid files from above are going after umount/mount
test -f $MPT/touchfile           && fail "touchfile should have disappeared"
test -f $MPT/pwd                 && fail "pwd file should have disappeared"
test -f $MPT/ddtest              || fail "ddtest file should have reappeared and become valid again"
${CLI} verify -S 1 -f $MPT/test1 || fail "ddfile should verify"

${CLI} fsck $MPT || fail "fsck should succeed - no cross links yet"

set +x
echo "*************************************************************************************"
echo "Test3 completed successfully"
echo "*************************************************************************************"
exit 0
