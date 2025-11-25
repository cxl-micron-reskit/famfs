#!/usr/bin/env bash
# NOTE: Do NOT use set -euo pipefail in files that are sourced.
#       This file is designed to be safe when loaded into any shell.

cwd=$(pwd)

# ---------------------------------------------------------------------
# Defaults (safe, no empty-string arguments)
# ---------------------------------------------------------------------
VG=()                        # No valgrind by default
SCRIPTS=../scripts
RAW_MOUNT_OPTS=("-t" "famfs" "-o" "noatime" "-o" "dax=always")
BIN=../debug
RMMOD=0
FAMFS_MOD="famfs.ko"

# Default subcommands
CREAT=("creat")
VERIFY=("verify")
CP=("cp")

# Environment overrides with safe defaults
DEV=${DEV:-/dev/dax0.0}
MPT=${MPT:-/mnt/famfs}
UMOUNT=${UMOUNT:-umount}
FAMFS_MODE=${FAMFS_MODE:-v1}
NODAX_ARG=${NODAX_ARG:-}     # empty or --nodax

# ---------------------------------------------------------------------
# Option parsing (safe: no strict mode)
# ---------------------------------------------------------------------
while (( $# > 0 )); do
    flag="$1"
    shift
    case "$flag" in
        -M|--module)
            FAMFS_MOD="$1"
            echo "FAMFS_MOD=${FAMFS_MOD}"
            shift
            ;;
        -d|--device)
            DEV="$1"
            shift
            ;;
        -b|--bin)
            BIN="$1"
            shift
            ;;
        -s|--scripts)
            SCRIPTS="$1"
            shift
            ;;
        -m|--mode)
            FAMFS_MODE="$1"
            shift
            ;;
        -D|--nodax)
            NODAX_ARG="--nodax"
            ;;
        -v|--valgrind)
            VG=("valgrind"
                "--leak-check=full"
                "--show-leak-kinds=all"
                "--track-origins=yes")

            # Valgrind-friendly test modes
            CREAT=("creat" "-t" "1")
            VERIFY=("verify" "-t" "1")
            CP=("cp" "-t" "0")
            ;;
        -n|--no-rmmod)
            # corrected: flag means "do NOT unload module"
            RMMOD=1
            ;;
        *)
            echo "Unrecognized command-line arg: $flag"
            ;;
    esac
done

# ---------------------------------------------------------------------
# Mode selection — safe array initialization
# ---------------------------------------------------------------------
if [[ "$FAMFS_MODE" == "v1" || "$FAMFS_MODE" == "fuse" ]]; then
    echo "FAMFS_MODE: $FAMFS_MODE"

    if [[ "$FAMFS_MODE" == "fuse" ]]; then
        MOUNT_OPTS=("--fuse")
        MKFS_OPTS=()
        FSCK_OPTS=()
        [[ -n "$NODAX_ARG" ]] && MKFS_OPTS+=("$NODAX_ARG")
        [[ -n "$NODAX_ARG" ]] && FSCK_OPTS+=("$NODAX_ARG")
    else
        MOUNT_OPTS=("--nofuse")
        MKFS_OPTS=()
        FSCK_OPTS=()
    fi
else
    echo "FAMFS_MODE: invalid"
    return 1 2>/dev/null || exit 1
fi

# ---------------------------------------------------------------------
# Final command arrays (created AFTER option parsing)
# ---------------------------------------------------------------------
MOUNT=( "sudo" "${VG[@]}" "$BIN/famfs" "mount" "${MOUNT_OPTS[@]}" )
MKFS=(  "sudo" "${VG[@]}" "$BIN/mkfs.famfs" "${MKFS_OPTS[@]}" )
CLI=(   "sudo" "${VG[@]}" "$BIN/famfs" )
CLI_NOSUDO=( "${VG[@]}" "$BIN/famfs" )
FSCK=(  "sudo" "${VG[@]}" "$BIN/famfs" "fsck" "${FSCK_OPTS[@]}" )

# RAW_MOUNT_OPTS, CREAT, VERIFY, CP remain as defined
