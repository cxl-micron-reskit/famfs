#!/usr/bin/env bash

set -euo pipefail

########################################
# Configuration (override via env)
########################################
RUN_ID="$(date +%Y%m%d_%H%M%S)"
MOUNT_DIR="${MOUNT_DIR:-/mnt/famfs}"
LOG_DIR="${LOG_DIR:-$PWD/perf/perf_logs_${RUN_ID}}"
WORK_DIR="${WORK_DIR:-$PWD/perf}"
LOG_FILE="${LOG_DIR}/famfs_perf_regression_${RUN_ID}.log"
SUDO="${SUDO:-sudo}"
SUMMARY_FILE="${SUMMARY_FILE:-$LOG_DIR/summary_${RUN_ID}.log}"
FAMFS_MODE="${FAMFS_MODE:-fuse}"

FAMFS_BIN_DIR="${FAMFS_BIN_DIR:-$PWD/debug}"

# Default device: /dev/dax0.0 (user can override by setting FAMFS_DEV in env)
FAMFS_DEV="${FAMFS_DEV:-/dev/dax0.0}"

FAMFS_BIN="${FAMFS_BIN:-${FAMFS_BIN_DIR:+$FAMFS_BIN_DIR/}famfs}"
MKFS_CMD="${MKFS_CMD:-${FAMFS_BIN_DIR:+$FAMFS_BIN_DIR/}mkfs.famfs}"
MKFS_KILL_CMD="${MKFS_KILL_CMD:-${FAMFS_BIN_DIR:+$FAMFS_BIN_DIR/}mkfs.famfs -f -k}"
MOUNT_CMD_OPT="${MOUNT_CMD_OPT:-${FAMFS_BIN_DIR:+$FAMFS_BIN_DIR/}famfs mount --$FAMFS_MODE}"
UMOUNT_BIN="${UMOUNT_BIN:-umount}"
FAMFS_MOUNT_CMD="${FAMFS_MOUNT_CMD:-$MOUNT_CMD_OPT $FAMFS_DEV $MOUNT_DIR}"

# famfs creat configuration
# Default assumes: famfs creat <path> <size-in-bytes>
FAMFS_CREATE_SUBCMD="${FAMFS_CREATE_SUBCMD:-creat}"

# mmap test size (GB) and IO sizes (CSV)
MMAP_SIZE_GB="${MMAP_SIZE_GB:-100}"
MMAP_IO_SIZES="${MMAP_IO_SIZES:-4K,64K,1M}"

# control max number of files
MAX_FILES="${MAX_FILES:-10000}"

# Drop caches before each measured test (careful on shared systems)
DROP_CACHES="${DROP_CACHES:-1}"

########################################
# Logging setup
########################################
mkdir -p "$LOG_DIR" "$WORK_DIR"
exec > >(tee -a "$LOG_FILE") 2>&1

echo "==== FAMFS Regression Run: ${RUN_ID} ===="
echo "Host: $(hostname)"
echo "Kernel: $(uname -a)"
echo "Mounts: $(mount | grep famfs || true)"
echo "Test dir: $MOUNT_DIR"
echo "Work dir: $WORK_DIR"
echo "Log file: $LOG_FILE"
echo "FAMFS_DEV: $FAMFS_DEV"
echo "FAMFS_BIN_DIR: ${FAMFS_BIN_DIR:-<not set>}"
echo "MKFS_CMD: $MKFS_CMD"
echo "MKFS_KILL_CMD: $MKFS_KILL_CMD"
echo "FAMFS_MOUNT_CMD: $FAMFS_MOUNT_CMD"
echo "FAMFS_MODE: $FAMFS_MODE"
echo "FAMFS_BIN: $FAMFS_BIN"
echo "FAMFS_CREATE_SUBCMD: $FAMFS_CREATE_SUBCMD"
echo "MMAP_SIZE_GB: $MMAP_SIZE_GB"
echo "MMAP_IO_SIZES: $MMAP_IO_SIZES"
echo "=========================================="

########################################
# Utilities
########################################
abort() { echo "[ERROR] $*" ; exit 1; }

check_cmd() { command -v "$1" >/dev/null 2>&1 || abort "Missing required command: $1"; }

measure() {
	local label="$1"; shift
	if [[ "$DROP_CACHES" == "1" ]]; then
		echo "[INFO] Dropping kernel caches prior to $label"
		echo 3 | $SUDO tee /proc/sys/vm/drop_caches >/dev/null
	fi
	local start_ns end_ns elapsed_ns elapsed_s rc
	start_ns=$(date +%s%N)
	"$@"
	rc=$?
	end_ns=$(date +%s%N)
	elapsed_ns=$((end_ns - start_ns))
	elapsed_s=$(awk -v ns="$elapsed_ns" 'BEGIN{printf "%.6f", ns/1e9}')
	echo "$label, elapsed_sec=$elapsed_s, rc=$rc"
	return "$rc"
}


ensure_mounted() {
# Require that MOUNT_DIR is mounted; optionally tighten to require famfs type.
	if ! mount | grep -q "on ${MOUNT_DIR}"; then
		echo "[WARN] ${MOUNT_DIR} is not currently mounted."
	abort "Fs not mounted"
	fi
}

reformat_and_mount() {
	echo "[REFORMAT] Attempting unmount, reformat and remount"
	$SUDO $UMOUNT_BIN "$MOUNT_DIR" || true
	if [[ -n "$FAMFS_DEV" ]]; then
		echo "[REFORMAT] Running: $SUDO $MKFS_KILL_CMD $FAMFS_DEV"
		$SUDO $MKFS_KILL_CMD "$FAMFS_DEV"
		echo "[REFORMAT] Running: $SUDO $MKFS_CMD $FAMFS_DEV"
		$SUDO $MKFS_CMD "$FAMFS_DEV"
		echo "[REMOUNT] Running: $SUDO $FAMFS_MOUNT_CMD"
		eval "$SUDO $FAMFS_MOUNT_CMD"
	else
		echo "[ERR] FAMFS_DEV not set; exiting... "
		abort "Famfs device not available, aborting..."
	fi
	ensure_mounted
}

# famfs creat wrapper
# Usage: famfs_create <path> <size_bytes>
famfs_create() {
	local fpath="$1"
	local fsize="$2"
	echo "[CREATE] $FAMFS_BIN $FAMFS_CREATE_SUBCMD -s $fsize $fpath"
	sudo $FAMFS_BIN $FAMFS_CREATE_SUBCMD -s $fsize $fpath
}

check_for_max_files() {
    local requested="$1"

    if (( requested > MAX_FILES )); then
        echo "$MAX_FILES"
    else
        echo "$requested"
    fi
}

# Create N files using famfs_create
# Usage: create_files <count> <prefix> <bytes>
create_files() {
	local count="$1" prefix="$2" bytes="$3"
	local i
	for i in $(seq 1 "$count"); do
		famfs_create "$MOUNT_DIR/${prefix}_$i" "$bytes"
	done
}

########################################
# Helper binaries — compile from PWD
########################################
ensure_helpers() {
	check_cmd gcc
	local src_dir="${SRC_DIR:-$PWD/perf}"

	[[ -f "${src_dir}/openclose_bench.c" ]] || abort "Missing ${src_dir}/openclose_bench.c"
	[[ -f "${src_dir}/mmap_bench_seq_rand.c" ]] || abort "Missing ${src_dir}/mmap_bench_seq_rand.c"

	echo "[BUILD] Compiling helper binaries from ${src_dir}"
	gcc -O2 -Wall -Wextra -o "${WORK_DIR}/openclose_bench" "${src_dir}/openclose_bench.c"
	gcc -O2 -Wall -Wextra -o "${WORK_DIR}/mmap_bench" "${src_dir}/mmap_bench_seq_rand.c"
}

########################################
# Test registry (plugin-friendly)
########################################
declare -a TEST_IDS=()
declare -a TEST_FUNCS=()
declare -a TEST_DESCS=()

register_test() {
	local id="$1" desc="$2" func="$3"
	TEST_IDS+=("$id")
	TEST_DESCS+=("$desc")
	TEST_FUNCS+=("$func")
}

run_registered_tests() {
	local n=${#TEST_IDS[@]}
	for ((i=0; i<n; i++)); do
		local id="${TEST_IDS[$i]}"
		local desc="${TEST_DESCS[$i]}"
		local func="${TEST_FUNCS[$i]}"
		echo
		echo "---- [${id}] ${desc} ----"
		"$func"
	done
}

########################################
# Individual tests (switched to famfs creat)
########################################

# 1) Create 1 file size 2M, measure time
test_001_create_1_file() {
# Begin with a clean filesystem
	reformat_and_mount
	ensure_mounted
	measure "CREATE_2M" famfs_create "$MOUNT_DIR/reg_test_2M" $((2 * 1024 * 1024))
}

# 2) Create 100 files, measure time
test_002_create_100_files() {
	reformat_and_mount
	ensure_mounted
	COUNT=$(check_for_max_files 100)
	if (( $COUNT < 100 )); then
		echo "Skipping CREATE_100, requested file count exceeds max files"
		return
	fi
	measure "CREATE_100" create_files "$COUNT" "t100" $((2 * 1024 * 1024))
}

# 3) Create 1000 files, measure time
test_003_create_1000_files() {
	reformat_and_mount
	ensure_mounted
	COUNT=$(check_for_max_files 1000)
	if (( $COUNT < 1000 )); then
		echo "Skipping CREATE_1000, requested file count exceeds max files"
		return
	fi
	measure "CREATE_1000" create_files "$COUNT" "t1000" $((2 * 1024 * 1024))
}

# 4) Create 10000 files, measure time
test_004_create_10000_files() {
	reformat_and_mount
	ensure_mounted
	COUNT=$(check_for_max_files 10000)
	if (( $COUNT < 10000 )); then
		echo "Skipping CREATE_10000, requested file count exceeds max files"
		return
	fi
	measure "CREATE_10000" create_files "$COUNT" "t10000" $((2 * 1024 * 1024))
}

# 6) Create max files, measure time
test_005_create_max_files() {
	reformat_and_mount
	ensure_mounted
	measure "CREATE_$MAX_FILES" create_files "$MAX_FILES" "t_$MAX_FILES" $((2 * 1024 * 1024))
}
# 6) Open and close the 10000 files
test_006_openclose_max_files() {
	# Do not reformat, re-use the files created in prev test
	ensure_mounted
	echo "[TEST] Open/Close $MAX_FILES files... "
	measure "OPENCLOSE_MAX_FILES" "${WORK_DIR}/openclose_bench" "$MOUNT_DIR" "t_$MAX_FILES" "$MAX_FILES"
}

test_007_mmap() {
	reformat_and_mount
	ensure_mounted
	measure "CREATE_${MMAP_SIZE_GB}" famfs_create "$MOUNT_DIR/mmap_${MMAP_SIZE_GB}GB.bin" $((${MMAP_SIZE_GB} * 1024 * 1024 * 1024))
	echo "[TEST] ${MMAP_SIZE_GB}GB mmap read/write bandwidth & IOPS (${MMAP_IO_SIZES})"
	measure "MMAP_${MMAP_SIZE_GB}GB" "${WORK_DIR}/mmap_bench" "$MOUNT_DIR/mmap_${MMAP_SIZE_GB}GB.bin" "$MMAP_IO_SIZES"
}

########################################
# Register tests
########################################
register_test "001" "Create 1 2MB file"                test_001_create_1_file
register_test "002" "Create 100 files"                 test_002_create_100_files
register_test "003" "Create 1000 files"                test_003_create_1000_files
register_test "004" "Create 10000 files"               test_004_create_10000_files
register_test "005" "Create $MAX_FILES files"          test_005_create_max_files
register_test "006" "Open/Close first 1000 files"      test_006_openclose_max_files
register_test "007" "${MMAP_SIZE_GB}GB mmap BW & IOPS" test_007_mmap

########################################
# Main
########################################
check_cmd awk
ensure_helpers
run_registered_tests

echo
echo "==== DONE (${RUN_ID}) ===="
echo "Log file at: $LOG_FILE , summary: $SUMMARY_FILE"
sync

sleep 1
sudo grep 'elapsed_sec'    "$LOG_FILE" >> "$SUMMARY_FILE"
sudo grep 'throughput' "$LOG_FILE" >> "$SUMMARY_FILE"
