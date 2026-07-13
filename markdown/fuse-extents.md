# Userspace roadmap: simple-fmaps (unroll interleave for GET_FMAP)

Status: initial draft, iterating. Companion to the kernel roadmap
`~/Documents/kernel-fuse-extents.md`.

## Goal

Teach `famfs_fused` to serve **simple-extent-only** fmaps to the kernel: when a
file's on-media fmap is interleaved, **unroll** it into a long, uniform-size
simple-extent list at GET_FMAP time. The famfs on-media metadata log is
**unchanged** (interleaved extents stay compact on media); only the wire form
sent to the kernel changes.

This pairs with a kernel that accepts simple-only fmaps, indexes uniform
extents by shifted offset (O(1)), and supports a large GET_FMAP response
buffer.

## Scope

In:
- `famfs_fused` unrolls interleaved fmaps -> uniform simple-extent lists on
  GET_FMAP (prototype already in `src/famfs_fmap.c`
  `famfs_log_file_meta_to_msg()`).
- A `--simple-fmaps` daemon option, plus automatic enablement when the daemon
  is in daxdev-push mode (the `FUSE_DEV_IOC_DAXDEV_OPEN` ioctl works), which
  marks the new simple-only kernel.
- A large GET_FMAP reply buffer (match the kernel's negotiated max).

Out (for now):
- Standalone (famfsv1) unroll when passing interleaved extents to the
  standalone kernel via ioctl -- deferred; note the hook.
- Any change to on-media log entries or the allocator's interleave output.
- Interleaved/stripe backing-devs (ceded to Miklos).

## The unroll (make it *uniform*)

The prototype emits one simple extent per chunk. To enable the kernel's O(1)
shift-index fast path, the emitter must:
- emit extents of a single `ext_size` = `ie_chunk_size` (power of two,
  PMD-aligned -- already true), last extent possibly shorter;
- set `nextents = ceil(file_size / ext_size)` and `ext_type = SIMPLE`.

There is **no wire UNIFORM flag or ext_size field** -- the kernel detects the
uniform extent size at GET_FMAP parse time (see the kernel roadmap), so the wire
header is unchanged. The emitter just has to *produce* a single-size list.

Per chunk `c`: `devindex = strips[c % nstrips].se_devindex`,
`offset = strips[c % nstrips].se_offset + (c / nstrips) * chunk_size`,
`len = min(chunk_size, file_size - c*chunk_size)`. (Matches the kernel stripe
math; already implemented in the test change.)

Contiguous (non-interleaved) files remain a single simple extent -- trivially
UNIFORM with `nextents == 1`.

## `famfs_fused` option + auto-detection

**No new capability bit.** Upstream will not grant a FUSE_INIT flag just to
signal "simple-only", so we reuse a probe the daemon already does: whether the
`FUSE_DEV_IOC_DAXDEV_OPEN` ioctl works. That ioctl and the interleave drop are
one ABI generation, co-delivered:

- `DAXDEV_OPEN` works => new famfs fuse ABI => simple-extent only (unroll).
- `DAXDEV_OPEN` fails (ENOTTY) => old kernel => interleave (+ GET_DAXDEV pull).

The daemon already runs this probe to choose daxdev push vs GET_DAXDEV pull
(`#ifdef FUSE_DEV_IOC_DAXDEV_OPEN` at compile time; `fuse_daxdev_open()` success
at run time, `src/famfs_fused.c`). Reuse its result to also select the fmap
format -- no new negotiation, no new flag.

- Add a daemon CLI option `--simple-fmaps` (force unroll on every GET_FMAP),
  parsed alongside the existing options in `src/famfs_fused.c`, for testing /
  forcing simple on any kernel (simple is always accepted).
- **Auto-enable**: force simple-fmaps whenever the daemon is in daxdev-push
  mode (i.e. `DAXDEV_OPEN` succeeded). No operator action needed.
- Precedence: `simple_fmaps = cli_opt || daxdev_push_mode`.

**Ordering falls out for free**: daxdevs must be registered before a file's
GET_FMAP is served (the kernel rejects fmaps referencing unregistered daxdevs),
so push-vs-pull is already known when any fmap is composed.

**Soundness caveat (invariant to preserve)**: `DAXDEV_OPEN` present must always
imply simple-only. The only broken combination is a kernel that drops interleave
*without* the ioctl -- do not ship that. Upstream this is automatic
(GET_DAXDEV+interleave = legacy pair; DAXDEV_OPEN+simple = new pair); in CI,
never build a simple-only kernel that lacks the ioctl.

## Max reply buffer

- Today the daemon replies from a `FMAP_MSG_MAX = 4096` buffer
  (`src/famfs_fused.c`). Raise it to the negotiated max (match the kernel's
  `max_fmap_size`); allocate per-GET_FMAP (or once, reused).
- The unroll already returns -EINVAL when it overflows the buffer; convert that
  into a clear, logged "file too large for simple-fmap at this chunk size"
  error naming the inode and the ceiling, since it is now an operational limit,
  not a bug.

## File-size ceiling (operational)

`max_file = ((buffer - header) / wire_ext_size) * ext_size`. With the negotiated
large buffer and coarse chunks this reaches TiB; document the exact number for
the shipped default. Levers: bigger `chunk_size` (coarser striping, linearly
larger ceiling), bigger negotiated buffer, and (future) paginated GET_FMAP.

## Interaction with the multi-daxdev roadmap

- The unrolled `se_devindex` values are the userspace-assigned,
  cluster-invariant indices from `markdown/multi-daxdev-roadmap.md` (index 0 =
  primary; ADD_DAXDEV entries assign 1..n). A multi-daxdev striped file unrolls
  to extents whose `se_devindex` varies across the strips -- exactly what the
  simple-extent format is meant to carry.
- No conflict with per-device bitmaps / allocation (Phases 6/10): the allocator
  still produces interleaved on-media extents; the unroll happens only at the
  wire boundary.

## Standalone (deferred)

`famfsv1` passes fmaps to the kernel via `FAMFSIOC_MAP_CREATE` (interleaved
today). A future option unrolls interleaved -> uniform simple extents before
the ioctl, so the standalone kernel can also drop interleave handling. Note the
hook; not in this pass.

## Phased plan (userspace)

1. **Uniform unroll**: finish the `famfs_fmap.c` prototype to guarantee
   single-`ext_size` output (no wire flag). Unit test.
2. **Buffer**: raise the daemon reply buffer to the negotiated max; clear
   error on overflow.
3. **CLI + auto-detect**: `--simple-fmaps` option; auto-enable from the
   daxdev-push-mode (`DAXDEV_OPEN`-works) probe.
4. **Log/format audit**: confirm no on-media change; confirm contiguous files
   still emit one simple extent.
5. **Standalone hook (deferred)**: unroll path for `FAMFSIOC_MAP_CREATE`.

## Touchpoints

- `src/famfs_fmap.c` `famfs_log_file_meta_to_msg()` -- the unroll (prototype
  landed on a test change).
- `src/famfs_fused.c` `famfs_get_fmap()` (~:725-784) -- reply buffer size
  (`FMAP_MSG_MAX`), calls the emitter.
- `src/famfs_fused.c` option parsing + the existing `DAXDEV_OPEN` push-mode
  flag -- the `--simple-fmaps` option and the reuse of push-mode as the
  simple-only auto-detect.
- On-media structs `src/famfs_meta.h` (`famfs_log_fmap`,
  `famfs_interleaved_ext`) -- unchanged; referenced only as the unroll input.

## Testing

- Unit: unroll of an interleaved fmap produces the right extent count, a single
  `ext_size`, and correct per-chunk `{devindex, offset, len}`; round-trips the
  kernel stripe math for sample offsets.
- Unit: contiguous file -> single simple extent.
- Unit: over-ceiling file -> clean error, not a crash.
- Smoke: mount + read/mmap a striped file end-to-end on a simple-only kernel;
  verify data matches the interleaved layout.

## Open questions

- **ext_size on the wire vs derived**: keep `se_len` per wire extent (simple,
  validated) or drop it for UNIFORM (compact, +33% ceiling)? Match the kernel
  decision.
- **Auto-detect signal**: RESOLVED -- reuse the `FUSE_DEV_IOC_DAXDEV_OPEN`
  push-mode probe the daemon already runs (no new capability bit). Invariant:
  `DAXDEV_OPEN` present must always imply simple-only (co-deliver them).
- **Fragmented contiguous files**: if an allocation is multiple contiguous runs
  of different sizes, it is not UNIFORM. Same open question as the kernel doc:
  guarantee single-extent files, split to uniform, or wait for the non-uniform
  path. Decide the emitter contract.
- **Buffer negotiation**: fixed raised max vs INIT-negotiated.
