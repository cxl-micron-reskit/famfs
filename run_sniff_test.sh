#!/usr/bin/env bash

CWD=$(pwd)
BIN="$CWD/debug"
SCRIPTS="$CWD/scripts"
MOUNT_OPTS="-t famfs -o noatime -o dax=always "
HARNESS="sniff"
TEST="sniff"
# Allow these variables to be set from the environment
if [ -z "$MPT" ]; then
    MPT=/mnt/famfs
fi
if [ -z "$DEV" ]; then
    DEV="/dev/dax0.0"
fi

# Check if we have password-less sudo, which is required
sudo -n true 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: password-less sudo capability is required to run sniff test"
    exit 1
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
	    LOG_CMDS="Y"
	    ;;
	(-r|--release)
	    BIN="$CWD/release"
	    echo "Using release BIN=$BIN"
	    ;;
	(-c|--coverage)
	    COVERAGE=1
	    BIN="$CWD/coverage"
	    echo "Using coverage BIN=$BIN"
	    ;;
	(-s|--sanitize)
	    COVERAGE=1
	    BIN="$CWD/sanitize"
	    echo "Using sanitize BIN=$BIN"
	    ;;
	(-v|--valgrind)
	    echo "run_sniff_test: valgrind mode"
	    VGARG="--valgrind"
	    ;;
	*)
	    echo "Unrecognized command line arg: $flag"
	    ;;
    esac
done

CLI="sudo $VG $BIN/famfs"

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
	    echo "Sniff test: testing famfs in v1 mode"
	else
	    echo "ERROR: famfsv1 was specified, but the kernel does not support it"
	    exit 1
	fi
    else
	if [ $(famfs_fuse_supported) -ge 1 ]; then
	    echo "Sniff test: testing famfs in fuse mode"
	else
	    echo "ERROR: famfs-fuse was specified, but the kernel does not support it"
	    exit 1
	fi
    fi
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

echo ":==*****************************************************************"
echo ":== run_sniff_test $(date)"
echo ":==MODE:     $FAMFS_MODE"
echo ":==CWD:      $CWD"
echo ":==BIN:      $BIN"
echo ":==SCRIPTS:  $SCRIPTS"
echo ":==*****************************************************************"
show_dax_config "$DEV"
echo ":==*****************************************************************"

scripts/chk_memdev.sh "$DEV" || fail "Bad memory device $DEV"

# Verify that this script is not running as root
if [[ $EUID -eq 0 ]]; then
    echo "ERROR:"
    echo "You have run the sniff test as root"
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

# Build common arguments array
SNIFF_ARGS=(-b "$BIN" -s "$SCRIPTS" -d "$DEV" -m "$FAMFS_MODE" -H sniff)
[[ ${#MOD_ARG[@]} -gt 0 ]] && SNIFF_ARGS+=("${MOD_ARG[@]}")
[[ -n "$VGARG" ]] && SNIFF_ARGS+=("$VGARG")
[[ -n "$NODAX_ARG" ]] && SNIFF_ARGS+=("$NODAX_ARG")
[[ -n "$LOG_ARG" ]] && SNIFF_ARGS+=("$LOG_ARG")
[[ "$COVERAGE" -eq 1 ]] && SNIFF_ARGS+=("--coverage")

# Run prepare.sh to clean up
./smoke/prepare.sh "${SNIFF_ARGS[@]}" || exit -1
echo ":== prepare success"

# Now run the actual sniff test
echo ":==*****************************************************************"
echo ":== Starting sniff test"
echo ":==*****************************************************************"

TEST="sniff_test"

# Create a small randomized file (4K)
SMALL_FILE="$MPT/sniff_small"
SMALL_SEED=123
SMALL_SIZE=4096

echo ":== Creating small randomized file ($SMALL_SIZE bytes, seed=$SMALL_SEED)"
expect_good $CLI creat -r -s $SMALL_SIZE -S $SMALL_SEED "$SMALL_FILE" -- "Create small file"

# Verify the small file
echo ":== Verifying small file"
expect_good $CLI verify -S $SMALL_SEED -f "$SMALL_FILE" -- "Verify small file"

# Copy the small file
SMALL_COPY="$MPT/sniff_small_copy"
echo ":== Copying small file"
expect_good $CLI cp "$SMALL_FILE" "$SMALL_COPY" -- "Copy small file"

# Verify the copy of the small file
echo ":== Verifying small file copy"
expect_good $CLI verify -S $SMALL_SEED -f "$SMALL_COPY" -- "Verify small file copy"

# Create a large randomized file (8M)
LARGE_FILE="$MPT/sniff_large"
LARGE_SEED=456
LARGE_SIZE=8388608  # 8M

echo ":== Creating large randomized file ($LARGE_SIZE bytes, seed=$LARGE_SEED)"
expect_good $CLI creat -r -s $LARGE_SIZE -S $LARGE_SEED "$LARGE_FILE" -- "Create large file"

# Verify the large file
echo ":== Verifying large file"
expect_good $CLI verify -S $LARGE_SEED -f "$LARGE_FILE" -- "Verify large file"

# Copy the large file
LARGE_COPY="$MPT/sniff_large_copy"
echo ":== Copying large file"
expect_good $CLI cp "$LARGE_FILE" "$LARGE_COPY" -- "Copy large file"

# Verify the copy of the large file
echo ":== Verifying large file copy"
expect_good $CLI verify -S $LARGE_SEED -f "$LARGE_COPY" -- "Verify large file copy"

# Cleanup
stop_on_crash sudo umount $MPT -- "umount should work"

set +x
echo ":==-------------------------------------------------------------------"
echo ":==run_sniff_test completed successfully ($(date))"
echo ":==-------------------------------------------------------------------"
exit 0
