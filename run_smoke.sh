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
	(-D|--nodax)
	    NODAX_ARG="-D"
	    ;;
	(-l|--log)
	    LOG_ARG="--log"
	    ;;
	(-e|--noerrors)
	    SKIP_ERRS=0
	    ;;
	(--jmg) # For debugging an intermittent segfault in test1.sh
	    SKIP_SHADOW_YAML=1
	    SKIP_TEST4=1
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
	(--justpcq)
	    SKIP_TEST0=1
	    SKIP_TEST1=1
	    SKIP_TEST2=1
	    SKIP_TEST3=1
	    SKIP_TEST4=1
	    #SKIP_PCQ=1
	    #SKIP_FIO=1
	    SKIP_SHADOW_YAML=1
	    SKIP_STRIPE_TEST=1
	    COVERAGE=0
	    ;;
	(--quick)
	    SKIP_PCQ=1
	    SKIP_FIO=1
	    ;;
	(--yaml)
	    # Just test0 and test_shadow_yaml
	    #SKIP_TEST0=1
	    SKIP_TEST1=1
	    SKIP_TEST2=1
	    SKIP_TEST3=1
	    SKIP_TEST4=1
	    SKIP_PCQ=1
	    SKIP_FIO=1
	    SKIP_ERRS=1
	    SKIP_STRIPE_TEST=1
	    ;;
	(--stripe)
	    # Just test0 and test_shadow_yaml
	    #SKIP_TEST0=1
	    SKIP_TEST1=1
	    SKIP_TEST2=1
	    SKIP_TEST3=1
	    SKIP_TEST4=1
	    SKIP_PCQ=1
	    SKIP_FIO=1
	    SKIP_ERRS=1
	    SKIP_SHADOW_YAML=1
	    #SKIP_STRIPE_TEST=
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
	(--with-pcq)
	    FORCE_PCQ=1
	    ;;
	(-s|--sanitize)
	    COVERAGE=1
	    BIN="$CWD/sanitize"
	    echo "hello sanitize BIN=$BIN"
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

# Check for KABI 42 (standalone famfs, no interleave support)
FAMFS_IOCTL_H="/usr/include/linux/famfs_ioctl.h"
if [ -f "${FAMFS_IOCTL_H}" ]; then
    if grep -q '^#define[[:space:]]\+FAMFS_KABI_VERSION[[:space:]]\+42$' ${FAMFS_IOCTL_H} ; then
	KABI_42=1
	echo KABI=42
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
    MOD_ARG=("--module" "famfs")
elif [ -f "${V1PATH}/famfsv1.ko" ]; then
    MOD_ARG=("--module" "famfsv1")
else
    MOD_ARG=()
fi

id=$(id -un)
grp=$(id -gn)

if [[ "$BIN" == *[[:space:]]* ]]; then
    fail "ERROR: the BIN path ($BIN) contains spaces!"
fi
if [[ "$SCRIPTS" =~ *[[:space:]]* ]]; then
    fail "ERROR: the SCRIPTS path ($SCRIPTS) contains spaces!"
fi

# Build common arguments array for smoke test scripts
SMOKE_ARGS=(-b "$BIN" -s "$SCRIPTS" -d "$DEV" -m "$FAMFS_MODE")
[[ ${#MOD_ARG[@]} -gt 0 ]] && SMOKE_ARGS+=("${MOD_ARG[@]}")
[[ -n "$VGARG" ]] && SMOKE_ARGS+=("$VGARG")
[[ -n "$NODAX_ARG" ]] && SMOKE_ARGS+=("$NODAX_ARG")
[[ -n "$LOG_ARG" ]] && SMOKE_ARGS+=("$LOG_ARG")
[[ "$COVERAGE" -eq 1 ]] && SMOKE_ARGS+=("--coverage")

# Arrays to track test timing
declare -a TEST_NAMES=()
declare -a TEST_TIMES=()
SMOKE_START_TIME=$(date +%s)

# Format seconds as MM:SS
format_time() {
    local secs=$1
    printf "%d:%02d" $((secs / 60)) $((secs % 60))
}

start_time=$(date +%s)
./smoke/prepare.sh "${SMOKE_ARGS[@]}" || exit -1
elapsed=$(($(date +%s) - start_time))
TEST_NAMES+=("prepare")
TEST_TIMES+=("$elapsed")
echo ":== prepare success ($(format_time $elapsed))"

if [ -z "$SKIP_TEST0" ]; then
    start_time=$(date +%s)
    ./smoke/test0.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test0")
    TEST_TIMES+=("$elapsed")
    echo ":== test0 success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test0 due to run_smoke options"
fi

if [ -z "$SKIP_SHADOW_YAML" ]; then
    start_time=$(date +%s)
    ./smoke/test_shadow_yaml.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test_shadow_yaml")
    TEST_TIMES+=("$elapsed")
    echo ":== test_shadow_yaml success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test_shadow_yaml due to run_smoke options"
fi

if [ -z "$SKIP_TEST1" ]; then
    start_time=$(date +%s)
    sudo ./smoke/test1.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test1")
    TEST_TIMES+=("$elapsed")
    echo ":== test1 success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test1 due to run_smoke options"
fi

if [ -z "$SKIP_TEST2" ]; then
    start_time=$(date +%s)
    ./smoke/test2.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test2")
    TEST_TIMES+=("$elapsed")
    echo ":== test2 success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test2 due to run_smoke options"
fi

if [ -z "$SKIP_TEST3" ]; then
    start_time=$(date +%s)
    ./smoke/test3.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test3")
    TEST_TIMES+=("$elapsed")
    echo ":== test3 success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test3 due to run_smoke options"
fi

if [ -z "$SKIP_TEST4" ]; then
    start_time=$(date +%s)
    ./smoke/test4.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test4")
    TEST_TIMES+=("$elapsed")
    echo ":== test4 success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test4 due to run_smoke options"
fi

if [ -z "$SKIP_ERRS" ]; then
    sleep "${SLEEP_TIME}"
    start_time=$(date +%s)
    ./smoke/test_errors.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test_errors")
    TEST_TIMES+=("$elapsed")
    echo ":== test_errors success ($(format_time $elapsed))"
else
    echo ":== skipping test_errors.sh because -n|--noerrors was specified"
fi

if [ -z "$SKIP_STRIPE_TEST" ] && [ -z "$KABI_42"  ]; then
    start_time=$(date +%s)
    ./smoke/stripe_test.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("stripe_test")
    TEST_TIMES+=("$elapsed")
    echo ":== stripe_test success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    if [ -z "$KABI_42" ]; then
	echo ":== Skipped stripe test due to caller options"
    else
	echo ":== Skipped stripe_test because KABI_42 does not support it"
    fi
fi

if [[ $COVERAGE -ne 1 ]] || [[ -n "$FORCE_PCQ" ]]; then
    if [ -z "$SKIP_PCQ" ]; then
	# XXX: get test_pcq running properly in coverage test mode
	start_time=$(date +%s)
	./smoke/test_pcq.sh "${SMOKE_ARGS[@]}" || exit -1
	sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
	elapsed=$(($(date +%s) - start_time))
	TEST_NAMES+=("test_pcq")
	TEST_TIMES+=("$elapsed")
	echo ":== test_pcq success ($(format_time $elapsed))"
	sleep "${SLEEP_TIME}"
    else
	echo ":== skipped test_pcq due to run_smoke options"
    fi
else
    echo ":== Skipped test_pcq due to coverage test (use --with-pcq to override)"
fi

if [ -z "$SKIP_FIO" ]; then
    start_time=$(date +%s)
    ./smoke/test_fio.sh "${SMOKE_ARGS[@]}" || exit -1
    sudo chown -R ${id}:${grp} $BIN # fixup permissions for gcov
    elapsed=$(($(date +%s) - start_time))
    TEST_NAMES+=("test_fio")
    TEST_TIMES+=("$elapsed")
    echo ":== test_fio success ($(format_time $elapsed))"
    sleep "${SLEEP_TIME}"
else
    echo ":== skipped test_fio due to run_smoke options"
fi

sudo umount $MPT

set +x
SMOKE_END_TIME=$(date +%s)
SMOKE_TOTAL=$((SMOKE_END_TIME - SMOKE_START_TIME))

echo ":==-------------------------------------------------------------------"
echo ":== Test Timing Summary"
echo ":==-------------------------------------------------------------------"
for i in "${!TEST_NAMES[@]}"; do
    printf ":==  %-20s %s\n" "${TEST_NAMES[$i]}" "$(format_time ${TEST_TIMES[$i]})"
done
echo ":==-------------------------------------------------------------------"
printf ":==  %-20s %s\n" "TOTAL" "$(format_time $SMOKE_TOTAL)"
echo ":==-------------------------------------------------------------------"
echo ":==run_smoke completed successfully ($(date))"
echo ":==-------------------------------------------------------------------"
if [[ "${FAMFS_MODE}" == "fuse" ]]; then
    echo "WARNING TEST DISABLED IN FUSE MODE: test_errs.sh"
    echo "WARNING: test_pcq.sh does not test --setmode under fuse"
fi
exit 0
#sleep 4
#./scripts/teardown.sh
