#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/debug"
SCRIPTS="$CWD/scripts"
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
SLEEP_TIME=2

# Allow these variables to be set from the environment
if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$DEV" ]; then
    DEV="/dev/dax0.0"
fi
if [ -z "$ERRS" ]; then
    ERRS=1
fi

# Check if we have password-less sudi, which is required
sudo -n true 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: password-less sudo capability is required to run smoke tests"
fi

TEST_FUNCS="$SCRIPTS/test_funcs.sh"
if [ ! -f $TEST_FUNCS ]; then
    echo "Can't source $TEST_FUNCS"
    exit -1
fi

while (( $# > 0)); do
    flag="$1"
    shift
    case "$flag" in
	(-d|--device)
	    DEV=$1
	    shift;
	    ;;
	(-f|--fuse)
	    FAMFS_MODE="fuse"
	    ;;
	(-F|--nofuse)
	    FAMFS_MODE="v1"
	    ;;
	(-e|--noerrors)
	    SKIP_ERRS=0
	    ;;
	(-E|--justerrors)
	    SKIP_TEST0=1
	    SKIP_TEST1=1
	    SKIP_TEST2=1
	    SKIP_TEST3=1
	    SKIP_TEST4=1
	    SKIP_PCQ=1
	    SKIP_FIO=1
	    ;;
	(-f|--justfio)
	    SKIP_TEST0=1
	    SKIP_TEST1=1
	    SKIP_TEST2=1
	    SKIP_TEST3=1
	    SKIP_TEST4=1
	    SKIP_PCQ=1
#	    SKIP_FIO=0
	    ;;
	(--quick)
	    SKIP_PCQ=1
	    SKIP_FIO=1
	    ;;
	(--yaml)
	    # Just test0 and test_shadow_yaml
	    SKIP_TEST0=1
	    SKIP_TEST1=1
	    SKIP_TEST2=1
	    SKIP_TEST3=1
	    SKIP_TEST4=1
	    SKIP_PCQ=1
	    SKIP_FIO=1
	    SKIP_ERRS=1
	    SKIP_STRIPE_TEST=1
	    ;;
	(-N|--nosleep)
	    SLEEP_TIME=0
	    ;;
	(-r|--release)
	    BIN="$CWD/release"
	    echo "hello release BIN=$BIN"
	    ;;
	(-c|--coverage)
	    COVERAGE=1
	    BIN="$CWD/coverage"
	    echo "hello coverage BIN=$BIN"
	    ;;
	(-v|--valgrind)
	    echo "run_smoke: valgrind mode"
	    VGARG="--valgrind"
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;
    esac
done

CLI="sudo $VG $BIN/famfs"

#BIN=debug
if [ ! -d $BIN ]; then
    echo "Can't find executables"
    exit -1
fi
if [ ! -x "$BIN/famfs" ]; then
    echo "famfs cli missing or not built in subdir $BIN"
    exit -1
fi

source $TEST_FUNCS

# if the mode was not explicitly set, figure out what mode we can run in
if [ -z "$FAMFS_MODE" ]; then
    # Mode was not specified; first choice is fuse, but try v1 if no fuse support
    if [ $(famfs_fuse_supported) -ge 1 ]; then
	echo "Defaulting to fuse mode"
	FAMFS_MODE="fuse"
    elif [ $(famfs_v1_supported) -eq 1 ]; then
	echo "Fuse mode not enabled; defaulting to v1 mode"
	FAMFS_MODE="v1"
    else
	echo "ERROR: kernel appears not to be famfs-enabled"
	exit 1
    fi
else
    # Mode was specified... check if the kernel supports it
    if [[ ${FAMFS_MODE} == "v1" ]]; then
	if [ $(famfs_v1_supported) -eq 1 ]; then
	    echo "Smoke: testing famfs in v1 mode"
	else
	    echo "ERROR: famfsv1 was specified, but the kernel does not support it"
	    exit 1
	fi
    else
	if [ $(famfs_fuse_supported) -ge 1 ]; then
	    echo "Smoke: testing famfs in fuse mode"
	else
	    echo "ERROR: famfs-fuse was specified, but the kernel does not support it"
	    exit 1
	fi
    fi
fi

echo ":==*****************************************************************"
echo ":== run_smoke.sh $(date)"
echo ":==MODE:     $FAMFS_MODE"
echo ":==CWD:      $CWD"
echo ":==BIN:      $BIN"
echo ":==SCRIPTS:  $SCRIPTS"
echo ":==ERRS:     $ERRS"
echo ":==*****************************************************************"

scripts/chk_memdev.sh "$DEV" || fail "Bad memory device $DEV"

# Verify that this script is not running as root
if [[ $EUID -eq 0 ]]; then
    echo "ERROR:"
    echo "You have run the smoke tests as root"
    echo "This does not work, as some of the tests assume they are running without root privileges"
    exit -1
else
    echo "Not running as root."
fi

# Verify that we are running with passwordless sudo, because that is a requirement
if sudo -l &>/dev/null; then
    echo "user has sudo privileges"
else
    echo ":==Error: this script requires sudo privileges"
    exit -1
fi

# Figure out the module name(s)
V1PATH="/lib/modules/$(uname -r)/kernel/fs/famfs"
if [ -f "${V1PATH}/famfs.ko" ]; then
    MOD_ARG="--module famfs"
elif [ -f "${V1PATH}/famfsv1.ko" ]; then
    MOD_ARG="--module famfsv1"
else
    MOD_ARG=""
fi

id=$(id -un)
grp=$(id -gn)

if [[ "$BIN" == *[[:space:]]* ]]; then
    fail "ERROR: the BIN path ($BIN) contains spaces!"
fi
if [[ "$SCRIPTS" =~ *[[:space:]]* ]]; then
    fail "ERROR: the SCRIPTS path ($SCRIPTS) contains spaces!"
fi

set -x
./smoke/prepare.sh  ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
echo ":== test0 prepare success"

if [ -z "$SKIP_TEST0" ]; then
    ./smoke/test0.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    echo ":== test0 success"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test0 due to run_smoke options"
fi

if [ -z "$SKIP_SHADOW_YAML" ]; then
    ./smoke/test_shadow_yaml.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    echo ":== test_shadow_yaml success"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test_shadow_yaml due to run_smoke options"
fi

if [ -z "$SKIP_TEST1" ]; then
    sudo ./smoke/test1.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    echo ":== test1 success"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test1 due to run_smoke options"
fi

if [ -z "$SKIP_TEST2" ]; then
    ./smoke/test2.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    echo ":== test2 success"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test2 due to run_smoke options"
fi

if [ -z "$SKIP_TEST3" ]; then
    ./smoke/test3.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    echo ":== test3 success"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test3 due to run_smoke options"
fi

if [ -z "$SKIP_TEST4" ]; then
    ./smoke/test4.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    echo ":== test4 success"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test4 due to run_smoke options"
fi

if [ -z "$SKIP_ERRS" ]; then
    sleep "${SLEEP_TIME}"
    ./smoke/test_errors.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    echo ":== test_errs success"
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
else
    echo ":== skipping test_errors.sh because -n|--noerrors was specified"
fi

if [ -z "$SKIP_STRIPE_TEST" ]; then
    ./smoke/stripe_test.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    echo ":== stripe_test success"
    sleep "${SLEEP_TIME}"
fi

if [[ $COVERAGE -ne 1 ]]; then
    if [ -z "$SKIP_PCQ" ]; then
	# XXX: get test_pcq running properly in coverage test mode
	./smoke/test_pcq.sh ${MOD_ARG} $VGARG \
			    -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" \
	    || exit -1
	echo ":== test_pcq success"
	sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
	sleep "${SLEEP_TIME}"
    else
	echo ":== skipped test_pcq doe to run_smoke options"
    fi
else
    echo ":== Skipped test_pcq due to coverage test (it's slow)"	
fi

if [ -z "$SKIP_FIO" ]; then
    ./smoke/test_fio.sh ${MOD_ARG} $VGARG -b "$BIN" -s "$SCRIPTS" -d $DEV  -m "$FAMFS_MODE" || exit -1
    echo ":== test_fio success"
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test_fio due to run_smoke options"
fi

sudo umount $MPT

set +x
echo ":==-------------------------------------------------------------------"
echo ":==run_smoke completed successfully ($(date))"
echo ":==-------------------------------------------------------------------"
if [[ "${FAMFS_MODE}" == "fuse" ]]; then
    echo "WARNING TEST DISABLED IN FUSE MODE: test_errs.sh"
    echo "WARNING TEST DISABLED IN FUSE MODE: test_pcq.sh"
    echo "FIX THESE!!"
fi
exit 0
#sleep 4
#./scripts/teardown.sh
