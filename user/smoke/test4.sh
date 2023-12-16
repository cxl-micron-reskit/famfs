#!/usr/bin/env bash

cwd=$(pwd)

# Defaults
DEV="/dev/pmem0"
VG=""
SCRIPTS=../scripts/
MPT=/mnt/famfs
BIN=../debug
KMOD=../../kmod

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
	(-k|--kmod)
	    KMOD=$1
	    shift
	    ;;
	(-v|--valgrind)
	    # no argument to -v; just setup for Valgrind
	    VG="valgrind --leak-check=full --show-leak-kinds=all"
	    ;;
	*)
	    remainder="$flag $1";
	    shift;
	    while (( $# > 0)); do
		remainder="$remainder $1"
		shift
	    done
	    echo "ignoring commandline remainder: $remainder"
	    ;;

    esac
done

echo "DEVTYPE=$DEVTYPE"
MKFS="sudo $VG $BIN/mkfs.famfs"
CLI="sudo $VG $BIN/famfs"
MULTICHASE="sudo $SCRIPTS/../../multichase/multichase"

source $SCRIPTS/test_funcs.sh
# Above this line should be the same for all smoke tests

set -x

verify_mounted $DEV $MPT "test2.sh"

${CLI} creat -s 3g  ${MPT}/memfile       || fail "can't create memfile for multichase"

# Let's count the faults during the multichase run
sudo sh -c "echo 1 > /sys/fs/famfs/fault_count_enable"

${MULTICHASE} -d ${MPT}/memfile -m 2900m || fail "multichase fail"

set +x
echo -n "pte faults:"
sudo cat /sys/fs/famfs/pte_fault_ct || fail "cat pte_fault_ct"
echo
echo -n "pmd faults: "
sudo cat /sys/fs/famfs/pmd_fault_ct || fail "cat pmd_fault_ct"
echo
echo -n "pud faults: "
sudo cat /sys/fs/famfs/pud_fault_ct || fail "cat pud_fault_ct"
echo

echo "*************************************************************************************"
echo "Test4 (multichase) completed successfully"
echo "*************************************************************************************"
