#!/usr/bin/env bash
#
# pmd_fault_check.sh - run pmd_fault_check under the fs_dax tracepoints.
#
# pmd_fault_check (test/pmd_fault_check.c) decides PMD-vs-PTE from the
# process's own fault count. This wrapper adds the kernel's view: it enables
# the fs_dax:dax_pte_fault and fs_dax:dax_pmd_fault_done tracepoints filtered
# on the file's inode, runs the helper, and checks the trace:
#
#   --expect pmd (default): exactly as many successful (NOPAGE) PMD faults as
#                           the helper predicted possible, no FALLBACK result
#                           on a 2 MiB window that lies entirely inside the
#                           mapping, and only as many dax_pte_fault events as
#                           the helper predicted for pages no PMD can cover
#                           (the tail of a file whose size is not a 2 MiB
#                           multiple)
#
# A FALLBACK on a partial window (the tail, or a head before the first 2 MiB
# boundary) is not a failure: the mm core calls the filesystem's huge_fault
# for any fault whose PMD slot is still empty without first checking that a
# PMD would fit in the VMA (that check is left to the fault handler), and the
# DAX PMD handler correctly declines with FALLBACK before the page gets its
# PTE fault. Whether that attempt happens at all depends on whether a
# neighbouring mapping already populated the shared PMD slot, so it comes and
# goes with mmap layout; the helper's own fault count is the same either way.
#   --expect pte:           no successful PMD fault, and one dax_pte_fault per
#                           page touched
#
# If tracefs or the fs_dax events are unavailable, the helper still runs and
# only its fault-count verdict applies (a warning says so).
#
# Usage: pmd_fault_check.sh [-b <dir>] [helper options...] <file>
#   -b <dir>   directory holding the pmd_fault_check binary (default: the
#              famfs build dir next to this script's tree, ../debug)
#   All other options are passed through to pmd_fault_check; --expect/-e is
#   also read here to pick the trace check.
#
# Needs root for tracefs; re-execs itself under sudo -n if not root.

set -u

if [[ $EUID -ne 0 ]]; then
    exec sudo -n "$0" "$@"
fi

HERE=$(cd "$(dirname "$0")" && pwd)
BINDIR="$HERE/../debug"
EXPECT="pmd"
ARGS=()

while (( $# > 0 )); do
    case "$1" in
        -b)
            BINDIR="$2"; shift 2 ;;
        -e|--expect)
            EXPECT="$2"; ARGS+=("$1" "$2"); shift 2 ;;
        --expect=*)
            EXPECT="${1#--expect=}"; ARGS+=("$1"); shift ;;
        *)
            ARGS+=("$1"); shift ;;
    esac
done

HELPER="$BINDIR/pmd_fault_check"
if [[ ! -x "$HELPER" ]]; then
    echo "pmd_fault_check.sh: helper not found: $HELPER" >&2
    exit 2
fi
if (( ${#ARGS[@]} == 0 )); then
    echo "pmd_fault_check.sh: no file given" >&2
    exit 2
fi
FILE="${ARGS[-1]}"
if [[ ! -f "$FILE" ]]; then
    echo "pmd_fault_check.sh: not a file: $FILE" >&2
    exit 2
fi

# Locate tracefs and the fs_dax events
TRACEFS=""
for d in /sys/kernel/tracing /sys/kernel/debug/tracing; do
    if [[ -d "$d/events/fs_dax" ]]; then
        TRACEFS="$d"
        break
    fi
done

if [[ -z "$TRACEFS" ]]; then
    echo "pmd_fault_check.sh: WARNING: fs_dax tracepoints unavailable; fault-count check only"
    exec "$HELPER" "${ARGS[@]}"
fi

INO=$(stat -c %i "$FILE")
EV_PTE="$TRACEFS/events/fs_dax/dax_pte_fault"
EV_PMD="$TRACEFS/events/fs_dax/dax_pmd_fault_done"
TRACE_OUT=$(mktemp /tmp/pmd_fault_trace.XXXXXX)

cleanup() {
    echo 0 > "$EV_PTE/enable" 2>/dev/null
    echo 0 > "$EV_PMD/enable" 2>/dev/null
    echo 0 > "$EV_PTE/filter" 2>/dev/null
    echo 0 > "$EV_PMD/filter" 2>/dev/null
    if [[ -n "${SAVED_BUF:-}" ]]; then
        echo "$SAVED_BUF" > "$TRACEFS/buffer_size_kb" 2>/dev/null
    fi
    rm -f "$TRACE_OUT"
}
trap cleanup EXIT

# A PTE run on a big file emits thousands of events; make sure they fit.
SAVED_BUF=$(cat "$TRACEFS/buffer_size_kb" 2>/dev/null)
if [[ "$SAVED_BUF" =~ ^[0-9]+$ ]] && (( SAVED_BUF < 16384 )); then
    echo 16384 > "$TRACEFS/buffer_size_kb"
else
    SAVED_BUF=""
fi

echo "ino == $INO" > "$EV_PTE/filter" || { echo "cannot set pte filter" >&2; exit 2; }
echo "ino == $INO" > "$EV_PMD/filter" || { echo "cannot set pmd filter" >&2; exit 2; }
echo > "$TRACEFS/trace"
echo 1 > "$EV_PTE/enable"
echo 1 > "$EV_PMD/enable"

HELPER_OUT=$(mktemp /tmp/pmd_fault_helper.XXXXXX)
"$HELPER" "${ARGS[@]}" | tee "$HELPER_OUT"
HRC=${PIPESTATUS[0]}

echo 0 > "$EV_PTE/enable"
echo 0 > "$EV_PMD/enable"
cat "$TRACEFS/trace" > "$TRACE_OUT"

if (( HRC == 2 )); then
    # usage/system error, or nothing to verify: no trace verdict
    rm -f "$HELPER_OUT"
    exit 2
fi

# The helper's prediction of what the kernel can do with this mapping.
# pages/pmd_possible/pte_only are decimal; off is printed with %#x, so it is
# "0" or "0x...", which bash arithmetic accepts as is.
field() { sed -n "s/.*[[:space:]]$1=\(0x[0-9a-fA-F]*\|[0-9]*\).*/\1/p" "$HELPER_OUT" | head -1; }
PAGES=$(field pages)
PMD_POSSIBLE=$(field pmd_possible)
PTE_ONLY=$(field pte_only)
OFF=$(field off)
rm -f "$HELPER_OUT"
for v in "$PAGES" "$PMD_POSSIBLE" "$PTE_ONLY"; do
    if [[ ! "$v" =~ ^[0-9]+$ ]]; then
        echo "pmd_fault_check.sh: could not parse helper output" >&2
        exit 2
    fi
done
if [[ ! "$OFF" =~ ^(0x[0-9a-fA-F]+|[0-9]+)$ ]]; then
    echo "pmd_fault_check.sh: could not parse helper output (off)" >&2
    exit 2
fi
PAGE_SIZE=$(getconf PAGESIZE)
(( PMD_PAGES = (2 * 1024 * 1024) / PAGE_SIZE ))
(( OFF_PG = OFF / PAGE_SIZE ))

PTE=$(grep -c 'dax_pte_fault:' "$TRACE_OUT")
PMD_OK=$(grep 'dax_pmd_fault_done:' "$TRACE_OUT" | grep -c 'NOPAGE')
PMD_OTHER=$(grep 'dax_pmd_fault_done:' "$TRACE_OUT" | grep -v -c -e NOPAGE -e FALLBACK)

# Sort the FALLBACK results by where they happened. The event's pgoff is the
# page that faulted; round it down to its 2 MiB window in the file (the VA and
# file colours match whenever a PMD was possible). A window that lies entirely
# inside the mapped range should have taken a PMD, so a FALLBACK there is a
# real failure; a FALLBACK on a window that only partly overlaps the mapping
# (head or tail) is the kernel declining a PMD that could never have fit.
PMD_FB=0       # fallbacks on full windows: failures
PMD_FB_EDGE=0  # fallbacks on partial head/tail windows: expected
while read -r pgoff; do
    [[ "$pgoff" =~ ^0x[0-9a-fA-F]+$ ]] || continue
    (( win = pgoff & ~(PMD_PAGES - 1) ))
    if (( win >= OFF_PG && win + PMD_PAGES <= OFF_PG + PAGES )); then
        (( PMD_FB++ ))
    else
        (( PMD_FB_EDGE++ ))
    fi
done < <(grep 'dax_pmd_fault_done:' "$TRACE_OUT" | grep 'FALLBACK' \
         | sed -n 's/.*[[:space:]]pgoff \(0x[0-9a-fA-F]*\).*/\1/p')

TRC=0
case "$EXPECT" in
    pmd)
        if (( PMD_OK != PMD_POSSIBLE || PTE != PTE_ONLY || PMD_FB != 0 || PMD_OTHER != 0 )); then
            TRC=1
        fi
        ;;
    pte)
        if (( PMD_OK != 0 || PTE != PAGES )); then
            TRC=1
        fi
        ;;
    *)
        echo "pmd_fault_check.sh: bad --expect '$EXPECT'" >&2
        exit 2
        ;;
esac

if (( TRC == 0 )); then
    TRES=PASS
else
    TRES=FAIL
fi
echo "pmd_fault_check.sh: trace ino=$INO pmd_ok=$PMD_OK/$PMD_POSSIBLE pte_faults=$PTE/$PTE_ONLY pmd_fallback=$PMD_FB pmd_fallback_edge=$PMD_FB_EDGE pmd_other=$PMD_OTHER pages=$PAGES expect=$EXPECT $TRES"

if (( TRC != 0 )); then
    echo "pmd_fault_check.sh: first fallback/pte events:"
    grep -e 'FALLBACK' -e 'dax_pte_fault:' "$TRACE_OUT" | head -5
fi

if (( HRC != 0 || TRC != 0 )); then
    exit 1
fi
exit 0
