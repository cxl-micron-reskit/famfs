# Multi-Daxdev Support: Transition Roadmap

## Context

famfs today supports exactly one dax device per filesystem.  Several
pieces of on-media format and in-memory plumbing already anticipate
multi-daxdev support and should be reused rather than re-invented:

- `se_devindex` in `struct famfs_simple_extent` (`src/famfs_meta.h:117`)
  is already in the on-media format and is asserted to be 0 today.
- `ts_sb_flags` plus `FAMFS_PRIMARY_SB` / `FAMFS_SECONDARY_SB`
  (`src/famfs_meta.h:72-73, 89`) are already defined but unused.
- The fuse daemon already carries `max_daxdevs` and a `daxdev_table`
  pointer on `struct famfs_ctx` (`src/famfs_fused.h:24-25`); only the
  hard-coded `MAX_DAXDEVS 1` (`src/famfs_fused.c:1243`) and
  `FAMFS_SUPERBLOCK_MAX_DAXDEVS 1` (`src/famfs_meta.h:47`) caps stand
  in the way.
- `famfs_get_daxdev()` (`src/famfs_fused.c:668-707`) already takes an
  index parameter; it just rejects anything non-zero at `:682-688`.

This document lays out a ten-phase transition that works for both the
fuse and standalone (famfsv1) paths.  Phases are numbered in dependency
order: each phase only depends on lower-numbered ones, so the list is
also a valid implementation order.  **One documented exception**: the
*standalone* half of Phase 4 (materializing `.meta/.superblock.N` with
`se_devindex=N`) needs Phase 7's `ADD_DAXDEV` ioctl to have registered
device N kernel-side first.  The fuse half of Phase 4 has no such
dependency, so Phase 4 still lands in order on the fuse path; the
standalone meta-file registration is gated on Phase 7.  See the
ordering notes in Phase 4 and Phase 7.

**Testability is a first-class organizing constraint, co-equal with
dependency order.**  Phase - and commit - boundaries are drawn so that
each step's functional change can be verified by tests that land *with
it*, at its own boundary, without depending on a later phase.  A step
whose change cannot be tested at its own boundary is mis-scoped and
must be re-split.  This is why, for example, Phase 1 pulls the pure
accumulation helper out ahead of the logplay wiring (so the
entries-to-table transform is unit-testable before Phase 4 exists),
Phase 6 is a behavior-preserving refactor gated on parity tests, and
Phase 9 is split into a Phase-3-free commit 1 and a resolver-dependent
commit 2.  Each phase below names the specific test(s) that must land
in the same PR; the Verification section states the requirement in
full, including which surfaces are necessarily covered by smoke tests
rather than unit tests (kernel-side ioctl behavior, the standalone
meta-file path, real sysfs scanning).

**Phase references in code comments are transitional scaffolding.**  It
is fine - encouraged - to tag a code comment with the phase that
introduces or will extend it, written as **`Multi-daxdev phase N`**
(qualified, so the reference is unambiguous in the tree; bare "Phase N"
is not) - e.g. `Multi-daxdev phase 1`, `TODO (Multi-daxdev phase 6/9)`.
But those tags are only useful *until the multi-daxdev transition is
complete*; afterward they are noise and can be swept out.  What endures
is a clear explanation of *what the code does and why* - so never let a
phase tag stand in for that explanation.  Roadmap phase numbers are a
map for the work in flight; the functional comments are the
documentation that has to outlive the map.

The expected dependency shape:

```
Phase 1  FAMFS_LOG_ADD_DAXDEV (keyed by dd_uuid)
         + __famfs_add_daxdev() appender
         + pure daxdev-table accumulation helper
   |--> Phase 4 (wires accumulation into logplay)
   |--> Phase 5 (CLI 'devadd' + mkfs multi-dev refactor)

Phase 2  primary_dev_uuid in superblock
   |--> Phase 5
   |--> Phase 9 (multi-device fsck)

Phase 3  daxdev UUID -> devname resolver (+ 'famfs daxdev' CLI)
   |--> Phase 4
   |--> Phase 7 (ADD_DAXDEV + GET_MAX_DAXDEV ioctls)
   |--> Phase 8 (daemon loads <shadow>/daxdevs/)
   |--> Phase 9 commit 2 (secondary->primary redirect)

Phase 4 --> Phase 6, Phase 8, Phase 9, Phase 10
Phase 5 --> Phase 7, Phase 8
Phase 6  per-daxdev bitmaps in famfs_locked_log
   |--> Phase 9, Phase 10
```

Reading the order top-down:

- **Phases 1-3 are independent foundations.**  Phase 1 introduces the
  log entry type (keyed by `dd_uuid`, not `dd_daxdev`, because device
  path strings are not invariant across hosts) **and** the pure helper
  that accumulates those entries into a `struct famfs_daxdev` table, so
  the entries-to-table transform is unit-testable before any logplay
  wiring exists.  Phase 2 introduces
  the superblock field that lets a secondary device be tied back to
  its filesystem's primary.  Phase 3 is the cross-cutting `uuid ->
  devname` resolver - a narrow daxdev-uuid lookup (**not** a
  `blkid`-style format prober), available as a library call and a small
  `famfs daxdev` CLI - that turns the invariant uuid back into a
  per-host `/dev/dax*` path.
- **Phases 4-5 are the plumbing that turns 1-3 into working
  features.**  Phase 4 wires Phase 1's accumulation helper into
  logplay so replay records the device list (to per-device
  `<shadow>/daxdevs/N` files for the fuse daemon to load; via the kernel
  ioctl for standalone),
  and materializes per-secondary `.meta/.superblock.<N>` meta files so
  the secondary superblocks are accessible just like the primary's
  `.meta/.superblock` is today.
  Phase 5 is the `devadd` CLI verb (plus `mkfs.famfs` taking multiple
  daxdevs) and the `__famfs_mkfs()` refactor needed to write a
  secondary superblock.
- **Phases 6-8 are the in-memory and surface-facing pieces.**  Phase 6
  splits the allocator's bitmap state per device.  Phase 7 adds the
  kernel-facing ioctl(s).  Phase 8 makes the fuse daemon load the
  logplay-written `<shadow>/daxdevs/N` files into its in-memory
  `daxdev_table`; how the daemon then delivers a daxdev to the kernel
  (pull `GET_DAXDEV` / push `fuse_daxdev_open`) is a separate,
  present-day concern that consumes the table.
- **Phase 9 is the fsck capstone**, shipped as two commits.  Commit 1
  makes multi-device fsck report overall and per-device used/free,
  revives the raw-mmap fsck path under `daxmode_required` (required
  because secondaries don't have logs, so a dummy mount can't fabricate
  one), and requires the primary as the by-device input (rejecting a
  secondary).  Commit 2 then accepts any member daxdev and redirects a
  secondary to its primary via the Phase 3 resolver - the only part of
  Phase 9 that depends on Phase 3.
- **Phase 10 changes the allocator policy.**  Phase 6 was a pure
  plumbing refactor that left allocation behavior at "always
  devindex 0"; Phase 10 finally puts the multi-device bitmap array
  to work.  Non-interleaved allocation picks a random daxdev and
  falls back to other devices on ENOSPC.  Interleaved allocation
  drops the `nbuckets` parameter entirely and stripes across the
  existing daxdevs in random order (one strip per device, up to
  `ndaxdevs`).

Each phase below states its Goal, its Touchpoints (file:line list),
its Notes, and the unit tests that must land in the same PR.  No PR
is feature-complete without its tests.

---

## Cross-cutting concerns

Three concerns span multiple phases and are stated once here rather
than repeated.  **The entire design must work for both the standalone
(famfsv1) and fuse paths.**  A shadow-tree-only approach is
insufficient: the shadow tree only exists for fuse mounts, so anything
that lives in the shadow tree needs a standalone equivalent.

### CLI surface

The user-facing verbs are collected here so the contract lives in one
place; the implementation lands in Phase 5.

- **`mkfs.famfs [opts] <primary-daxdev> [<secondary-daxdev> ...]`** -
  multiple positional daxdevs.  `mkfs.famfs` is a standalone binary
  (`src/mkfs.famfs.c`), not a row in `famfs_cli_cmds[]`; today it
  takes exactly one positional (`daxdev = argv[optind++]` at
  `src/mkfs.famfs.c:132`).  The change collects `argv[optind..argc)`
  and updates `print_usage()` (`src/mkfs.famfs.c:27`).  Semantics:
  mkfs the primary as today, then for each secondary run the **same**
  secondary-superblock-write + `ADD_DAXDEV`-append that `devadd`
  uses (one shared lib entry point - see Phase 5).
- **`famfs devadd <target> <daxdev>`** - adds a daxdev to an existing
  filesystem.  New row in `famfs_cli_cmds[]` (`src/famfs_cli.c:2505`).
  Works online (against a mounted fs, log lock held via `flock` at
  `src/famfs_fused.c:1265`) and offline.  `<target>` identifies the
  filesystem (mount point or primary daxdev); `<daxdev>` is the device
  to add.
- **`famfs daxdev <resolve|list|refresh>`** - a thin CLI over the
  Phase 3 resolver library, for ops/debug (resolve one uuid, dump the
  cached map, force a rescan).  Detailed in Phase 3.
- **Deferred: UUID input.**  The `mkfs.famfs` / `devadd` verbs take
  **devnames** for now.  Accepting a daxdev by UUID (`--uuid`) on the
  command line is a follow-up: it requires the resolver (Phase 3) to
  turn the UUID into a local devname before the device can be opened.
  Recording it here so the devname-now / uuid-later split is in one
  place instead of implied per-phase.

### Path-agnostic in-memory daxdev table

Several phases (4, 6, 8, 9) need an in-memory list of the filesystem's
daxdevs - at minimum `{ dd_uuid, dd_size, dd_daxdev }` per device.
The element type already exists and is path-agnostic: `struct
famfs_daxdev` (`src/famfs_meta.h:64`), the same struct embedded
on-media as `ts_daxdev` (`src/famfs_meta.h:90`).

Today an in-memory table exists **only** on the fuse side:
`daxdev_table` / `max_daxdevs` on `struct famfs_ctx`
(`src/famfs_fused.h:24-25`), allocated at `src/famfs_fused.c:1322`.
The standalone path has no such table - `grep` finds zero references
in `src/famfs_lib.c`.  This is the central standalone/fuse parity gap.

**Resolution**: a pure per-entry helper that adds one
`FAMFS_LOG_ADD_DAXDEV` entry to a `struct famfs_daxdev` array (Phase 1),
plus a whole-log walker built on it that fills the array from the
primary plus every `ADD_DAXDEV` entry (Phase 4).  Both operate on a
plain array + count, so they are path-agnostic and unit-testable.
*Where* the filled table lives differs by path, because their
lifecycles differ:

- **fuse**: logplay and the daemon are **separate processes** that
  communicate through the shadow tree, **not** shared memory - the
  daemon never calls logplay.  So the walker cannot fill the daemon's
  memory directly.  Instead, logplay runs the walker and serializes the
  result to daemon-private, **per-device** files **above**
  `<shadow>/root` - `<shadow>/daxdevs/N` (one file per devindex N, the
  access key, mirroring `.meta/.superblock.N`), each holding that
  device's `{ uuid, size }`.  Each file is published **atomically** by
  writing a temp file in the same directory and `rename()`-ing it into
  place, so **no lock is needed**: a reader sees a complete file or
  none, and because the `index -> uuid -> size` binding is invariant
  (log-derived) a re-publish of N is a byte-identical idempotent replace
  - concurrent writers never conflict (different N -> different files;
  same N -> same bytes).  The long-lived daemon **loads** these into its
  in-memory `famfs_ctx.daxdev_table` (exists today) - at startup (a
  `readdir` of `<shadow>/daxdevs/`), and lazily when it first needs an
  index it doesn't yet have (i.e. after a `devadd` + logplay) by opening
  `<shadow>/daxdevs/N` - resolving each uuid to a devname via the
  resolver cache.  Phase 4 is the writer; Phase 8 is the reader.
- **standalone**: there is no long-lived daemon and no shadow tree;
  each one-shot CLI invocation that needs the table builds it on demand
  by running the walker over the log.  Its home is `struct
  famfs_locked_log` (`src/famfs_lib_internal.h:33-56`), co-located with
  the per-device bitmaps Phase 6 adds there - so the allocator
  (Phase 6) and `famfs_fsck_scan()` (Phase 9), which already hold a
  `locked_log`, build and read the table there.  Phase 4's standalone
  logplay does **not** keep a userspace table; its device knowledge is
  persisted kernel-side via the `ADD_DAXDEV` ioctl (Phase 7) plus the
  `.meta/.superblock.N` files.

No phase may source the device count from `famfs_ctx` alone, nor assume
logplay can write the daemon's memory.  The fuse daemon's count comes
from the `<shadow>/daxdevs/` directory; the standalone count comes from
the walker over the log.

### Enabler: raw read/write on famfs-mode devices

Reading *or writing* a superblock today is mode-dependent.  A devdax
device can be raw-mmap'd directly; a device in **famfs mode that is
not part of a live mount** cannot - the only ways to touch its
superblock are to (a) flip it back to devdax mode and mmap (intrusive;
needs save/restore), or (b) do a famfs dummy mount.  This cost shows
up across the design:

- **Phase 3's resolver** scanner reads `ts_dev_uuid` off candidate
  devices.
- **Phase 9's fsck** switches a device to devdax precisely because
  raw mmap doesn't work in famfs mode.
- **mkfs / `devadd`** (Phase 5) write a superblock; today the write
  side is why mkfs has to route through `famfs_mkfs_via_dummy_mount()`
  (defined `src/famfs_lib.c:5601`, selected over `famfs_mkfs_rawdev()`
  at the `no_raw_dax` branch `src/famfs_lib.c:5811`) instead of a raw
  write when the device is in famfs mode.

**Work item (kernel/fsdev side)**: add raw **read and write** support
to famfs/fsdev dax mode, so a superblock can be read from *and written
to* a device in famfs mode without flipping to devdax or
dummy-mounting.  Concrete consumers this enables:

- `famfs fsck` uses raw **read** when the daxdev is in famfs mode and
  not part of an active mount (Phase 9).
- `mkfs.famfs` (and the `devadd` secondary-superblock write) uses raw
  **write** when the device is in famfs mode and not mounted (Phase 5),
  retiring the dummy-mount write path for that case.

This is a shared **enabler**, not a hard prerequisite: the interim
designs in Phases 3, 5, and 9 work without it (via the devdax-flip +
restore discipline, or the existing dummy mount), and simplify to a
direct read/write once it lands.  Those phases reference this item.

---

## Phase 1 - `FAMFS_LOG_ADD_DAXDEV` log entry

**Goal**: introduce a log record that identifies a secondary daxdev by
uuid (size + intended index) so logplay can rebuild the daxdev table
on every node, **and** the pure helper that turns a sequence of those
records into a `struct famfs_daxdev` table.  **The log entry must key
on `dd_uuid`, not `dd_daxdev`.**  The device path string is per-host
(`/dev/dax0.0` on one node may be `/dev/dax1.2` on another); the uuid
is the only invariant that survives log replay on a different system.
Resolving the uuid back to a local path is Phase 3's job.

**Why the accumulation helper lands here, not in Phase 4**: the
transform "ADD_DAXDEV entries -> `struct famfs_daxdev` array" has no
dependency on `__famfs_logplay()`, the resolver, meta files, or the
shadow/standalone split - it reads only the on-media structs this
phase introduces.  Landing it here makes it unit-testable in isolation
the moment the entry type exists (create entries, replay into a table,
verify), instead of only through Phase 4's heavyweight
full-`logplay()` integration test.  Phase 4 becomes pure *wiring*:
call this helper from logplay, fill devnames via the resolver,
materialize meta files.

**Touchpoints**:

- Insert `FAMFS_LOG_ADD_DAXDEV` into `enum famfs_log_entry_type`
  (`src/famfs_meta.h:162-167`) **before** the terminal
  `FAMFS_LOG_INVALID` sentinel.
- Add a new payload struct (`struct famfs_log_add_daxdev`) alongside
  `famfs_log_mkdir` / `famfs_log_file_meta`
  (`src/famfs_meta.h:179-198`).  Carry `u64 dd_size`, `uuid_le
  dd_uuid`, `u32 dd_index`.  Do **not** carry `dd_daxdev` (the path) -
  or if it's included, mark it as a hint only authoritative on the
  system that issued the add.
  - **On-media / ABI type convention**: every new OMF (on-media) and
    ioctl-ABI struct field uses an explicitly-sized type
    (`u64`/`u32`/`uuid_le`), **never `size_t`** - `size_t` is
    word-size-dependent and these structs are read across hosts and
    architectures.  This is why `dd_size` is `u64`, not `size_t`.
    (The pre-existing `struct famfs_daxdev.dd_size` is `size_t`
    (`src/famfs_meta.h:64`) - a latent portability bug to fix
    separately, not in this phase.)
- Add the union arm in `struct famfs_log_entry`
  (`src/famfs_meta.h:200-208`).
- Bump on-media format: `FAMFS_OMF_VER_MINOR` (`src/famfs_meta.h:62`)
  for an additive change; bump `FAMFS_OMF_VER_MAJOR` (`:61`) only if
  old userspace must refuse to read it.  Bump `FAMFS_CURRENT_VERSION`
  (`:41`) regardless.
- **Pure accumulation helper** (the path-agnostic daxdev table from
  Cross-cutting concerns): a function that consumes one
  `FAMFS_LOG_ADD_DAXDEV` entry (or walks a whole log) and claims the
  next `struct famfs_daxdev` slot from its `dd_uuid` / `dd_size` /
  `dd_index`, e.g.
  `int famfs_daxdev_table_add(struct famfs_daxdev *table, int *ndevs, int max, const struct famfs_log_entry *le);`.
  Operates on a plain `struct famfs_daxdev` array + count - **not** on
  `famfs_ctx` or `famfs_locked_log` - so both paths and the unit test
  share it.  Lives in `src/famfs_dax.c` (the daxdev module), not
  `famfs_lib.c`: it is a pure daxdev-table helper with no coupling to the
  log-append machinery.  Slot 0 (the primary, superblock-derived) is the
  caller's to seed; this helper only appends secondaries.  Define the
  duplicate-`dd_uuid` / conflicting-`dd_index` behavior here (reject vs
  last-wins) since the test pins it.  The chosen behavior is **reject**,
  with dense in-order indices (entry N occupies slot N): `-EINVAL` for a
  non-`ADD_DAXDEV` entry, `-ENOSPC` on overflow, `-ERANGE` for an
  out-of-order / non-dense `dd_index`, `-EEXIST` for a duplicate
  `dd_uuid`.
- **Log-append helper `__famfs_add_daxdev()`**: a function that builds a
  `FAMFS_LOG_ADD_DAXDEV` entry (`dd_uuid` / `dd_size` / `dd_index`) and
  appends it via `famfs_append_log()` (`src/famfs_lib.c:2475`, which
  stamps the seqnum and CRC).  Model it on the existing per-type append
  helpers `famfs_log_dir_creation()` / `famfs_log_file_creation()`
  (`src/famfs_lib.c:2603, 2550`).  **Stays in `famfs_lib.c`** (unlike the
  pure table helper above): it is coupled to the `static`
  `famfs_append_log()` / `famfs_log_full()`, so moving it would mean
  exporting the log-append machinery.  This is the appender the unit tests
  use to produce *real* (validly-CRC'd) entries, and the one Phase 5's
  `famfs_add_daxdev()` wraps with devname->uuid resolution.  **Do not
  export `famfs_gen_log_entry_crc()` for the tests** - the append path
  already stamps the CRC, so tests build entries through this helper
  rather than hand-stamping CRCs.
- **Graceful arms at the *other* dispatch sites.**  Every
  `switch (famfs_log_entry_type)` must acknowledge the new type, or a
  stray `ADD_DAXDEV` entry looks like corruption.  There are three such
  switches; the real *behavior* (the logplay handler) is deferred to
  Phase 4, but the other two get graceful arms **now** so the type is
  handled everywhere the moment it exists:
  - `famfs_build_bitmap()` (`src/famfs_alloc.c:197`): add a
    `case FAMFS_LOG_ADD_DAXDEV:` that **skips** - the entry allocates no
    space, exactly like `FAMFS_LOG_MKDIR`.  Without it the `default:`
    arm prints a spurious "bad type" to stderr for every secondary
    daxdev during fsck (Phase 9) and on every allocation once such
    entries exist.
  - `famfs_dump_logentry()` (`src/famfs_misc.c:100`): add a case that
    prints the index / size / uuid instead of "unrecognized log entry
    type".
  - (`__famfs_logplay()` (`src/famfs_lib.c:1753`) is the third; its real
    arm lands in Phase 4.)
  Stats accounting (`struct famfs_log_stats`) gains no daxdev bucket
  here - an optional `dd_logged` counter fits Phase 9's per-device
  reporting.

**Notes**: CRC handling already covers the whole entry via
`famfs_gen_log_entry_crc()` (called from `famfs_append_log()` per the
existing pattern at `src/famfs_lib.c:2408`); no separate CRC work
needed, and nothing new needs to be exported.  Old readers will hit
the `default:` arm in the logplay switch (`src/famfs_lib.c:1989`) and
log "invalid log entry" - acceptable for a forward-incompatible bump,
less so for a backward-compatible one.

**Unit tests (same PR)**: two tests, both in `test/famfs_unit.cpp`.

- `TEST(famfs, famfs_log_add_daxdev_entry)` - the entry through the
  append path.  Model on `TEST(famfs, famfs_log)`
  (`test/famfs_unit.cpp:741`), which builds a mocked log (in memory /
  fd-backed) and appends entries via the log-creation helpers.  Append
  an `ADD_DAXDEV` entry with `__famfs_add_daxdev()` (so the seqnum and
  CRC are stamped by `famfs_append_log()`, **not** hand-computed),
  then assert it round-trips through `famfs_validate_log_entry()`, that
  a corrupted CRC fails validation, and that the OMF version constants
  moved as documented.  (Note: the existing `TEST(famfs,
  famfs_log_test)` at `:1819` is the *logging/syslog* test, not a log
  test - misleadingly named.  A rename to `famfs_logging_test` makes
  sense but must be a **separate commit**, never folded into the
  Phase 1 implementation.)
- `TEST(famfs, famfs_daxdev_table_accumulate)` - the pure helper.  This
  one needs no valid CRCs (accumulation reads only `dd_uuid` /
  `dd_size` / `dd_index`), so it hand-builds `famfs_log_entry` structs
  directly.  Seed slot 0 with a primary `struct famfs_daxdev`, build N
  `FAMFS_LOG_ADD_DAXDEV` entries, feed them through
  `famfs_daxdev_table_add()`, and assert: the table fills in encounter
  order; each slot's `dd_uuid` / `dd_size` match the entry; the count
  is `N+1`; overflow is rejected with `-ENOSPC`; a non-dense index with
  `-ERANGE`; a duplicate uuid with `-EEXIST`; a wrong entry type with
  `-EINVAL`.  This is the early, scaffolding-free "entries -> table"
  coverage; Phase 4 layers the full-`logplay()` integration (meta
  files, idempotency, shadow/standalone) on top.

---

## Phase 2 - primary-device uuid in secondary superblocks

**Goal**: a secondary device's superblock carries the uuid of the
*primary* device so a secondary can be tied back to its filesystem on
discovery.  Reuse the existing `ts_sb_flags` machinery rather than
adding a new flag.

**Touchpoints**:

- Add `uuid_le ts_primary_dev_uuid` (or similar) to `struct
  famfs_superblock` (`src/famfs_meta.h:77-91`), placed before `ts_crc`
  (`:88`) so it's CRC-covered.
- Mark the superblock kind by setting `FAMFS_PRIMARY_SB` /
  `FAMFS_SECONDARY_SB` in `ts_sb_flags` (`src/famfs_meta.h:72-73,
  89`).  Today `__famfs_mkfs()` (`src/famfs_lib.c:5458-5548`) leaves
  `ts_sb_flags` zero; the primary path should set `FAMFS_PRIMARY_SB`.
  The secondary path (Phase 5) sets `FAMFS_SECONDARY_SB` and populates
  `ts_primary_dev_uuid`.
- **A secondary superblock references no log**: the log lives only on
  the primary, so on a secondary `ts_log_offset` and `ts_log_len`
  (`src/famfs_meta.h:78-79`) are 0.  The Phase 5 write helper enforces
  this; `famfs_check_super()` can treat a secondary with a non-zero
  `ts_log_offset` as corrupt.
- Same version bump as Phase 1 (one OMF bump can cover both phases if
  they land together).

**Notes**: enables a sanity check at logplay/mount time - a secondary
superblock's `ts_primary_dev_uuid` must match the primary's
`ts_dev_uuid` (`src/famfs_meta.h:86`).  The check itself belongs in
`famfs_check_super()` (referenced from `src/famfs_lib.c:5483`).

**Unit tests (same PR)**: extend `TEST(famfs, famfs_super_test)`
(`test/famfs_unit.cpp:149`) with four cases - primary superblock has
`FAMFS_PRIMARY_SB` set, `ts_primary_dev_uuid` zero, and a non-zero
`ts_log_offset`; secondary has `FAMFS_SECONDARY_SB` set,
`ts_primary_dev_uuid` populated, and `ts_log_offset` / `ts_log_len`
zero; `famfs_check_super()` rejects a secondary whose
`ts_primary_dev_uuid` doesn't match the running primary's
`ts_dev_uuid`; and `famfs_check_super()` rejects a secondary that
carries a non-zero `ts_log_offset`.

---

## Phase 3 - daxdev UUID -> devname resolver

**Goal**: given a famfs filesystem UUID and a target device UUID,
return the local `/dev/dax*` path that backs that device on this host.
This is the piece that ties the invariant (uuid) used in the log and
on-media format back to the per-host devname needed to actually mmap
or ioctl the device.  Lands before its consumers - Phases 4, 7, 8, and
Phase 9 commit 2 translate log-entry / superblock uuids back to local
device paths (Phase 5 uses it only as a sanity check).  See the
Consumers list below.

**Not a `blkid`.**  The name similarity would overpromise: `blkid` /
`libblkid` enumerate *all* block devices and probe *many*
superblock/format magic numbers.  This is the opposite - a narrow,
one-way lookup: famfs daxdev uuid -> local dax devname, only when there
is a match, over a small set of dax devices.  No format probing, no
general enumeration.

**Two surfaces**: a **library call** (the core; consumers below call
it directly) and a thin **`famfs daxdev` CLI** for ops/debug/smoke -
a small verb family:

- `famfs daxdev resolve <fs_uuid> <dev_uuid>` - print the devname (or
  non-zero exit on miss); scriptable wrapper over
  `famfs_resolve_daxdev_uuid()`.
- `famfs daxdev list` - dump the cached map for a filesystem.
- `famfs daxdev refresh` - force a rescan via
  `famfs_refresh_daxdev_map()` (handy after device churn).

The CLI is a wrapper only - all logic lives in the library so the
internal consumers share it.

**Direction**: the resolver does **uuid -> devname**, and only that
direction is hard.  The devname-in verbs (`mkfs.famfs`, `devadd`)
already have a local devname and do not need the resolver to operate.
The resolver exists for the **reverse** problem: a log entry or
secondary superblock carries an invariant `dd_uuid`, and a *different
node* replaying that log (Phase 4) or the fuse daemon resolving a
non-zero index it has loaded into its table (Phase 8) must find which
local `/dev/dax*` currently
backs that uuid.  Devnames are per-host; the uuid is the only thing
that survives the trip.

### Two eras

This resolver is built twice behind one API:

- **Interim era (now)**: the DCD sysfs interface is not settled, so
  the resolver learns each device's uuids by **reading its superblock
  manually** - raw-mmap on a devdax device, or (once the Cross-cutting
  "raw read/write on famfs-mode devices" enabler lands) a
  direct read while the device is in famfs mode.  Results are cached
  in a host-local file (see below).
- **DCD era (future)**: the kernel exposes a uuid per devname through
  sysfs (likely indexed for lookup).  The scan collapses to reading
  that attribute - no mmap, no mode flips - and deallocated daxdev
  uuids **disappear from sysfs on their own**, which retires most of
  the cache-hygiene machinery below.  The public entry points stay the
  same; only the implementation behind them changes.

### Cache location (host-local, never in the mount)

The uuid -> devname mapping is **host-local**: different nodes legitimately
disagree on the devname for the same uuid.  It therefore must **never**
live under `.meta/` - `.meta` is inside the mount and replicates to
every node that mounts the filesystem, which is exactly wrong for
host-local data.

- **fuse**: `<shadow>/daxdev_map.yaml` - a sibling of the mirrored
  `root/` subtree under the shadow root (resolved via
  `famfs_get_shadow_root()`, `src/famfs_lib.c:1725`), outside the tree
  that becomes the mount.  Host-local; torn down with the shadow tree.
- **standalone**: `/var/lib/famfs/<fs_uuid>/daxdev_map.yaml`.

The file format is a YAML list of `{ fs_uuid, dev_uuid, devname,
last_seen_at }` records.  This is throwaway state: corruption or
deletion just triggers a rescan.

### Cache hygiene

Test environments create and destroy filesystems thousands of times,
so the cache must not accumulate stale uuids.  Three mechanisms, all
interim-era only (DCD-era sysfs self-cleans):

1. **Validate-on-resolve**: a cache hit is trusted only after a cheap
   recheck - the cached devname still exists *and* (when readable
   without an intrusive mode flip) its superblock's `ts_dev_uuid`
   still equals the cached `dev_uuid` and `ts_uuid` the cached
   `fs_uuid`.  On mismatch, evict the row and fall through to a
   rescan.
2. **Destroy-time eviction**: whenever a superblock is overwritten,
   evict the old uuid from the cache.  `mkfs.famfs` is the
   create/destroy path (`-k`/`-f`): before stamping the new
   superblock it reads the device's old `ts_uuid`/`ts_dev_uuid` and
   drops the stale entry (and, for standalone, removes a now-empty
   `/var/lib/famfs/<old_fs_uuid>/`).  This is the deterministic answer
   to test churn.
3. **TTL prune on refresh**: each `famfs_refresh_daxdev_map()` rewrites
   the file dropping records not re-seen this pass and older than a
   TTL (`last_seen_at`), bounding growth of a live cache.

No orphan-GC sweep: deciding whether a `<fs_uuid>/` dir is orphaned
requires re-reading device uuids - the exact intrusive operation we
are avoiding - and DCD-era sysfs makes it moot.

**Touchpoints**:

- Lives in `src/famfs_dax.c` (declared in `famfs_lib.h` alongside the
  other daxdev entry points), **not** a new file.  The resolver's core
  operation - read a daxdev's superblock to learn its uuid - is gated on
  the mode management (`famfs_get_daxdev_mode()` /
  `famfs_set_daxdev_mode()`) that already lives there, so co-locating
  keeps the save/restore discipline in one place.  (`famfs_dax.c` is the
  daxdev module: mode ops, the Phase 1 `famfs_daxdev_table_add()` helper,
  and now the resolver.  If it later sprawls past ~1000 lines, split
  then - not pre-emptively.)  Entry points:
  - `int famfs_resolve_daxdev_uuid(const uuid_le *fs_uuid, const
    uuid_le *dev_uuid, char *path_out, size_t path_len);` returns 0 on
    hit, `-ENOENT` on miss, `-EEXIST` (or similar) on duplicate-uuid
    corruption.
  - `int famfs_refresh_daxdev_map(const uuid_le *fs_uuid, int
    verbose);`
- New `famfs daxdev` CLI verb (row in `famfs_cli_cmds[]`,
  `src/famfs_cli.c:2505`), a thin wrapper over the two entry points
  with `resolve` / `list` / `refresh` sub-actions (see Goal).  No logic
  in the CLI - it just formats output and exit codes.
- Reuse `famfs_get_daxdev_mode()` (`src/famfs_dax.c:66`) to gate
  access and `famfs_set_daxdev_mode()` (`src/famfs_dax.c:154`) to
  drive a probed device to devdax mode when it isn't already and
  isn't bound by a live famfs.  Do **not** call
  `famfs_check_or_set_daxmode()` at `src/famfs_dax.c:264` - that
  helper hard-codes the target to `DAXDEV_MODE_FAMFS` at `:304`,
  which is the wrong direction.
- Reuse `famfs_mmap_superblock_and_log_raw()` (`src/famfs_lib.c:855`)
  for the actual superblock read.
- **Safety constraint**: the scanner must skip any daxdev that is
  currently bound by a mounted famfs (regardless of its current
  mode).  Pre-check via `famfs_get_mpt_by_dev()`
  (`src/famfs_mount.c:69`, declared at
  `src/famfs_lib_internal.h:100`) before any mode change.  Mode
  changes are restored on the way out - same discipline as
  `famfs_fsck()` per `markdown/daxmode-analysis.md`.
- **Reading the superblock is mode-dependent** - call this out in a
  top-of-file comment.  Devdax: raw-mmap directly.  Famfs mode, not
  mounted: until the raw read/write enabler (Cross-cutting concerns) lands,
  flip to devdax + read + restore, gated on the same caller-supplied
  `set_daxmode` discipline used elsewhere; once the enabler lands,
  read directly in famfs mode.  Famfs mode, mounted: skipped by the
  safety constraint above.

**Consumers (later phases)**:

- Phase 4's **standalone** logplay arm calls
  `famfs_resolve_daxdev_uuid()` to get the devname for the `ADD_DAXDEV`
  ioctl arg.  (The fuse logplay path does *not* resolve - it writes only
  `{ uuid, size }` to per-device `<shadow>/daxdevs/N`, and the daemon
  resolves on load, Phase 8.)
- Phase 5's `devadd` CLI uses the resolver only as a sanity check -
  the operator supplied a devname directly, but the resolver can
  confirm `ts_dev_uuid` matches what's about to be logged.
- Phase 7's ioctl is invoked by userspace, so userspace runs the
  resolver before constructing the arg struct.
- Phase 8's daemon loader resolves each daxdev's uuid to a local devname
  (falling back to the resolver on a cache miss) when filling the
  daxdev-table slot's `dd_daxdev`.
- Phase 9 commit 2's fsck resolves a secondary's `ts_primary_dev_uuid`
  to the primary devname (the secondary->primary redirect), and
  resolves each secondary `dd_uuid` for unmounted by-device superblock
  validation.

**Notes**:

- "Just for DCD" framing: a non-DCD daxdev (no allocation tag) is
  still supported - its uuid is whatever mkfs generated and stamped
  at `src/famfs_lib.c:5520`.  The scanner doesn't care about tag vs.
  generated; it reads whatever is in `ts_dev_uuid`.
- Failure modes worth flagging: stale cache entry (devname no longer
  exists - handled by validate-on-resolve), duplicate uuids across
  devices (corruption - log loudly and refuse to resolve), and the
  device-in-famfs-mode read case above.

**Unit tests (same PR)**: add `TEST(famfs, famfs_daxdev_resolve_cache)`
that seeds a fake cache file and asserts `famfs_resolve_daxdev_uuid()`
returns the cached devname without touching `/sys`.  Add `TEST(famfs,
famfs_daxdev_resolve_miss)` that points the resolver at an empty cache
and a controllable mock `/sys/bus/dax/devices/` tree (or a test-only
entrypoint that bypasses `/sys`), and asserts a miss returns `-ENOENT`
without crashing.  Add `TEST(famfs, famfs_daxdev_resolve_evict)` that
seeds a cache entry and asserts the destroy-time eviction helper
removes exactly the overwritten uuid and leaves siblings intact.  The
`/sys`-walking refresh path and the `famfs daxdev` CLI (which is a thin
wrapper) are best left to smoke tests under `smoke/`, since
unit-testing real sysfs scanning is fragile.

---

## Phase 4 - wire daxdev accumulation into logplay

**Goal**: wire Phase 1's pure accumulation helper into
`__famfs_logplay()` and do the parts that *do* depend on logplay -
materializing meta files and recording the device list for the fuse
daemon.  Phase 1 already turns `FAMFS_LOG_ADD_DAXDEV` entries into a
`struct famfs_daxdev` table; this phase makes replay (a) on the
**fuse** path, run the walker and serialize the device list to
per-device `<shadow>/daxdevs/N` files (above `<shadow>/root`, lock-free
atomic publish) so the daemon can load them - logplay and the daemon
are separate processes
and do **not** share memory (see Cross-cutting concerns); and (b) on
**both** paths, materialize a `.meta/.superblock.<N>` meta file mapping
the secondary daxdev's superblock region.  The device list is keyed by
uuid; the local devname is resolved by the daemon (Phase 8) and by
standalone consumers (Phases 6/9), not stored in `daxdev_table`.  The
**meta-file creation** and the **`<shadow>/daxdevs/N` writes** are
the fuse path's logplay work; the **standalone** path instead registers
each device with the kernel via the `ADD_DAXDEV` ioctl (Phase 7) and
builds its userspace table on demand later (Phases 6/9), keeping no
table file.

**Meta-file naming (decision)**: secondary superblocks are exposed at
`<mount>/.meta/.superblock.<N>`, **index only** - no uuid in the name.
The primary stays at `<mount>/.meta/.superblock` (== index 0, no
suffix) for backward compatibility.  Rationale:

- `N` *is* the access key: it equals `se_devindex` and the
  daxdev-table slot, so "an extent references devindex N" maps to a
  direct, O(1) open of `.superblock.N` - no globbing, no name parsing.
- The uuid is already authoritative inside the superblock
  (`ts_dev_uuid`), and the index<->uuid binding is the log's job (the
  `ADD_DAXDEV` entry for index N).  Putting it in the filename would be
  a third copy of that binding that can disagree with the other two.
- Validation needs no uuid in the name: a reader cross-checks the
  *content's* `ts_dev_uuid` against the log entry for index N, and a
  secondary's `ts_primary_dev_uuid` against the primary's
  `ts_dev_uuid`.  A uuid in the filename would be a redundant fourth
  copy, not a new check.
- `.superblock.N.<uuid>` was considered and rejected: it forces a
  wildcard match instead of a direct open and lets two files claim the
  same index with different uuids.  (Same index-keyed spirit as
  Phase 8's `<shadow>/daxdevs/N`.)

These `.superblock.N` files are the **mounted** read path (and how the
fuse daemon reaches secondary superblocks).  Unmounted by-device fsck
has no meta files; it discovers secondaries from the primary's log,
resolves each uuid->devname, and raw-mmaps each device's superblock
directly (Phase 9).  Both routes land on the same set of superblocks.

**Touchpoints**:

- Add a `case FAMFS_LOG_ADD_DAXDEV:` arm in `__famfs_logplay()`
  (declared at `src/famfs_lib_internal.h:140-145`, defined at
  `src/famfs_lib.c:1690`; switch starts at `src/famfs_lib.c:1753`,
  current arms end with `case FAMFS_LOG_MKDIR` at `:1909`, `default:`
  at `:1989`).  This is the **real** handler; the other two
  `famfs_log_entry_type` switches (`famfs_build_bitmap`,
  `famfs_dump_logentry`) already got graceful arms in Phase 1.  The arm:
  1. **fuse**: add the device to the in-memory walker array via
     Phase 1's `famfs_daxdev_table_add()` (`dd_uuid` / `dd_size`), then
     publish `<shadow>/daxdevs/N` via atomic temp+`rename()` (no lock).
     **No devname resolution here** - each file stores only
     `{ uuid, size }` (devindex is the filename N), and the daemon
     resolves uuid->devname when it loads the file (Phase 8).
     **standalone**: resolve `dd_uuid` -> local devname via the Phase 3
     resolver (needed for the ioctl arg; a miss blocks registration of
     that device), then issue the `ADD_DAXDEV` ioctl (Phase 7).  The
     ioctl is **idempotent** - a harmless no-op if the kernel already
     knows the device - so re-running logplay is safe with no
     coordination.  Step 2 then creates `.meta/.superblock.N`, the
     *first* file to reference device N; doing the ioctl **before** that
     create is what makes it correct (device N is registered before any
     `se_devindex=N` extent is mapped - see the ordering constraint in
     Notes).  As a pure **optimization** (not required for correctness,
     since the ioctl is idempotent), query `FAMFSIOC_GET_MAX_DAXDEV`
     once at the start of replay and skip the ioctl for indices the
     kernel already knows.  No userspace table is kept here.
  2. Materializes `.meta/.superblock.<N>` by calling the refactored
     `__famfs_mkmeta_superblock()` (see below).  Passes the `shadow`
     flag through from the enclosing `__famfs_logplay()` call so it
     works for both the standalone and fuse paths.
- Refactor `__famfs_mkmeta_superblock()`
  (`src/famfs_lib.c:1174-1282`) to take `int devindex` and a
  destination relpath instead of hardcoding `.meta/.superblock`.  The
  existing call sites (`famfs_mkmeta_standalone()` at
  `src/famfs_lib.c:1432`, `src/famfs_mount.c:863, 1152`) become
  explicit `(devindex=0, ".meta/.superblock", ...)`.  New
  per-secondary calls from the logplay arm pass `(devindex=N,
  ".meta/.superblock.N", ...)`.  The extent stamped into the meta
  file is `{ se_devindex=N, se_offset=0,
  se_len=FAMFS_SUPERBLOCK_SIZE }` - superblock size is the same on
  every daxdev, so only the devindex and path change.
- Lift the caps: `FAMFS_SUPERBLOCK_MAX_DAXDEVS 1`
  (`src/famfs_meta.h:47`) and `MAX_DAXDEVS 1`
  (`src/famfs_fused.c:1243`) to a real value (e.g. 64).  Cost is
  `sizeof(struct famfs_daxdev) * cap` ~= 88 bytes per slot.
- The accumulated table is the **path-agnostic daxdev table** from
  Cross-cutting concerns.  Add the whole-log walker - built on Phase 1's
  per-entry `famfs_daxdev_table_add()` - in **`src/famfs_dax.c`**, next
  to that helper.  It is a pure daxdev-table helper with no log-machinery
  coupling, so it stays out of `famfs_lib.c` (which is ~6k lines and
  should not grow gratuitously); the Phase 4 logplay arm there only
  *calls* it.  The `<shadow>/daxdevs/N` serialize/parse helpers (one
  device per file) live in `famfs_dax.c` too, shared by the Phase 4
  writer and the Phase 8 reader.
  - **fuse**: logplay runs the walker and, per discovered device,
    publishes `<shadow>/daxdevs/N` (above `<shadow>/root`) by writing a
    temp file in that directory and `rename()`-ing it into place -
    atomic, **lock-free**, idempotent (a re-publish is a byte-identical
    replace).  It does **not** write `famfs_ctx.daxdev_table` directly -
    that lives in the *daemon's* address space, a different process.  The
    daemon (Phase 8) loads the files into `famfs_ctx.daxdev_table` /
    `max_daxdevs` (`src/famfs_fused.h:24-25`); its slot-0 allocation at
    `src/famfs_fused.c:1322-1325` just needs to grow.  No flock on either
    side: atomic rename gives all-or-nothing visibility, and per-device
    files have no shared read-modify-write to serialize.
  - **standalone**: Phase 4 logplay does *not* keep a userspace table or
    a table file - it registers devices kernel-side via the
    `ADD_DAXDEV` ioctl (Phase 7) and materializes `.meta/.superblock.N`.
    The standalone in-memory table is built on demand later, by Phase 6
    (locked-log init) and Phase 9 (fsck), each running the same walker
    over the log into a `struct famfs_locked_log`-resident array.  Do
    not source `ndaxdevs` from `famfs_ctx`, which does not exist there.

**Notes**:

- Must be idempotent - replay runs on every mount on every node, and
  also at fuse-daemon startup.  Existing `__famfs_mkmeta_superblock()`
  already handles the "file exists, NOP" case
  (`src/famfs_lib.c:1208-1226, 1258-1261`); the per-secondary path
  inherits that.
- **Standalone ordering constraint**: the non-shadow path of
  `__famfs_mkmeta_superblock()` invokes `famfs_v1_set_file_map()`
  (`src/famfs_lib.c:1265`) which ioctls the kernel to register a file
  mapping with `se_devindex=N`.  `.meta/.superblock.N` is the **first**
  file to reference device N, and the kernel won't accept that mapping
  until Phase 7's `FAMFSIOC_ADD_DAXDEV` has registered device N.  So
  per discovered device the arm does, serialized: `ADD_DAXDEV` ioctl
  (idempotent) **then** the meta-file create.  That single ordering
  rule covers correctness; the `GET_MAX_DAXDEV` skip is only an
  optimization on top of it.
- **Fuse ordering constraint**: the fuse path has no logplay->kernel
  registration step (shadow files are plain files in the shadow tree),
  but it has a producer-side ordering of its own.  The daemon learns a
  secondary device only from `<shadow>/daxdevs/`, and it must already
  hold N's file by the time it serves a file that references device N -
  otherwise it cannot resolve N.  Since a reference to N is reachable
  only through a file that contains `se_devindex=N`, logplay must make
  the `<shadow>/daxdevs/N` file durably visible **before** it creates any
  shadow file - meta (`.superblock.N`) or regular - that references N.
  In a single replay this falls out of log order (the `ADD_DAXDEV` entry
  for N precedes any file on N) **only if** the `<shadow>/daxdevs/N` file
  is published in the `ADD_DAXDEV` arm, not batched to end-of-replay.
  This is the fuse analog of the standalone "`ADD_DAXDEV` before
  `set_file_map`" rule above.
- Duplicate-uuid / conflicting-index behavior is decided and tested in
  Phase 1 (it lives in `famfs_daxdev_table_add()`); this arm just
  propagates whatever error the helper returns.

**Unit tests (same PR)**: two tests.

- `TEST(famfs, famfs_daxdev_walker)` - the whole-log walker in
  isolation, no logplay.  Synthesize a log buffer (primary + N
  `FAMFS_LOG_ADD_DAXDEV` entries), run the walker into a `struct
  famfs_daxdev` array, and assert the table matches the entries in
  order with the right count.  This is the pure seam that Phases 6 and
  9 also depend on, so it is tested here directly rather than only
  through logplay.
- `TEST(famfs, famfs_logplay_multi_daxdev)` - the logplay integration.
  Model on `TEST(famfs, famfs_log_overflow_files)`
  (`test/famfs_unit.cpp:1157`).  Synthesize a log with the primary plus
  N `FAMFS_LOG_ADD_DAXDEV` entries; drive `__famfs_logplay()` with the
  existing `shadow=1`, `dry_run=0` path against a tmpdir (so the shadow
  files are actually created on disk); assert:
  1. `<shadow>/daxdevs/1..N` (above `<shadow>/root`) exist, one per
     device, each holding the right `{ uuid, size }` keyed by its
     filename index - this is what logplay hands the daemon,
     since logplay does not touch the daemon's `famfs_ctx`.
  2. `.meta/.superblock.1`, `.meta/.superblock.2`, ... exist under the
     shadow root and contain `famfs_log_file_meta` YAML with
     `se_devindex=N` and `se_offset=0`.
  3. Running the same logplay twice produces identical results
     (idempotency) - both the `daxdevs/N` files and the meta files.

---

## Phase 5 - `mkfs.famfs` multi-daxdev and the `devadd` CLI verb

**Goal**: write a secondary-device superblock and append an
`ADD_DAXDEV` log entry on the primary - exposed two ways: a new
`famfs devadd` verb against an existing filesystem, and multiple
positional daxdevs on `mkfs.famfs`.  See Cross-cutting concerns / CLI
surface for the user-facing contract.

**One shared lib entry point.**  `mkfs.famfs`'s per-secondary handling
and `devadd` do the identical work - write the secondary superblock,
append `ADD_DAXDEV` - so factor it into a single function
(e.g. `famfs_add_daxdev(fs_target, devname, ...)`) that both call.
`mkfs.famfs` calls it in a loop after creating the primary; `devadd`
calls it once.

**Touchpoints**:

- `mkfs.famfs` (`src/mkfs.famfs.c`): change the single positional
  (`daxdev = argv[optind++]` at `:132`) to collect
  `argv[optind..argc)`; mkfs the first as the primary, then call the
  shared `famfs_add_daxdev()` for each remaining devname.  Update
  `print_usage()` (`:27`) to show the new positional list.
- New row in `famfs_cli_cmds[]` (`src/famfs_cli.c:2505-2522`):
  `{"devadd", do_famfs_cli_devadd, famfs_devadd_usage}`.  The CLI
  help-printing loop at `src/famfs_cli.c:2545` and lookup at `:2580`
  need no change beyond adding the row.  `<target>` is a mount point
  or primary daxdev; `<daxdev>` is the device to add.
- Factor superblock-init out of `__famfs_mkfs()`
  (`src/famfs_lib.c:5458-5548`, specifically the field-fill at
  `:5494-5527`) into a helper that takes a "primary or secondary"
  parameter.  The primary path keeps `ts_sb_flags = FAMFS_PRIMARY_SB`;
  the secondary path sets `FAMFS_SECONDARY_SB` plus
  `ts_primary_dev_uuid` from the running fs.
- **A secondary superblock carries no log.**  The log lives only on
  the primary, so the secondary path must write `ts_log_offset = 0`
  (and `ts_log_len = 0`) - `ts_log_offset` "offset to the start of the
  log file" and `ts_log_len` ("Primary log length")
  (`src/famfs_meta.h:78-79`) are meaningless on a secondary.  Only the
  primary
  superblock specifies the location and length of the log.  The
  secondary-vs-primary helper must zero these on the secondary path
  rather than copying the primary's values.
- The shared `famfs_add_daxdev()` helper takes a local **devname** on
  input, raw-mmaps the device's superblock (which the secondary-mkfs
  step just wrote), reads `ts_dev_uuid` (`src/famfs_lib.c:5520` is where
  the primary path generates it), and then **delegates the log append
  to Phase 1's `__famfs_add_daxdev()`**, passing *that uuid* (not the
  devname), the size, and the index.  It does not construct the entry
  or touch the CRC itself - `__famfs_add_daxdev()` /
  `famfs_append_log()` own that.  So `famfs_add_daxdev()` is "resolve
  devname -> uuid + open the fs", and `__famfs_add_daxdev()` is "append
  the entry".

**Notes**:

- Must work online (against a mounted fs, where the master holds the
  log lock via `flock` configured at `src/famfs_fused.c:1265`) and
  offline.
- **Deferred: UUID input.**  Both `devadd` and `mkfs.famfs` take
  devnames only in this phase.  A `--uuid` form is a follow-up that
  runs the Phase 3 resolver to turn the uuid into a local devname
  before opening the device; the shared helper above is unchanged
  because it already keys the log entry on `ts_dev_uuid`.
- **Superblock write is mode-dependent.**  Writing the secondary
  superblock follows the same devdax-vs-famfs-mode split as primary
  mkfs: a famfs-mode-unmounted device routes through
  `famfs_mkfs_via_dummy_mount()` today.  The Cross-cutting
  "raw read/write on famfs-mode devices" enabler lets that case use a
  raw write instead, retiring the dummy mount for it.

**Unit tests (same PR)**: add `TEST(famfs, famfs_mkfs_secondary)` to
exercise the factored superblock-write helper in isolation (in-memory
buffer, no real device - model on `TEST(famfs, famfs_mkfs)` at
`test/famfs_unit.cpp:104`).  Assert that the secondary path produces a
superblock with `FAMFS_SECONDARY_SB`, the given
`ts_primary_dev_uuid`, and a valid CRC, and that
`famfs_check_super()` accepts it.  End-to-end CLI behavior belongs in
`smoke/`.

---

## Phase 6 - per-daxdev bitmaps in `famfs_locked_log`

**Goal**: split the allocator's in-memory free-space tracking so there
is one bitmap per daxdev rather than one bitmap covering the whole
filesystem.  Prerequisite for the allocator ever returning a
`famfs_simple_extent` with `se_devindex != 0`, and for the per-device
fsck reporting in Phase 9.

New struct (lives in `src/famfs_lib_internal.h` alongside `struct
famfs_locked_log`):

```c
/* We're moving toward multi-device support; each device has a bitmap. */
struct famfs_dev_bitmap {
    u8  *bitmap;
    u64  nbits;
    /* Next-free high-water mark on this device.  famfs allocates but
     * never frees, so cur_pos is monotonic within a locked_log
     * session and the next allocation on this device can resume from
     * here.  Every allocator path that lands a bit on this device
     * must advance cur_pos past the last bit it set - see Phase 10.
     */
    u64  cur_pos;
};
```

**Semantics callout on `cur_pos`**: today's single `lp->cur_pos`
(`src/famfs_lib_internal.h:46`) is updated by the contiguous path
(`bitmap_alloc_contiguous()` at `src/famfs_alloc.c:384-385` writes
through `&lp->cur_pos`) but **not** by the bucketized interleaved
path.  The strided allocator at `src/famfs_alloc.c:663-670` uses a
local `pos` and never writes back to `lp->cur_pos`, and at `:610` it
sets `lp->cur_pos = 0` before falling back to contig.  So in today's
code the field is a fiction once an interleaved alloc has run.
Splitting per device makes the field's meaning crisp again - one
monotonic high-water mark per daxdev - but only if Phase 10's
interleaved path is updated to maintain it (see Phase 10
touchpoints).

**Touchpoints**:

- `struct famfs_locked_log` (`src/famfs_lib_internal.h:33-56`): drop
  the inline `bitmap` / `nbits` / `cur_pos` fields (`:38-46`) and
  replace with `struct famfs_dev_bitmap **dev_bitmaps;` plus an `int
  n_dev_bitmaps;`.  Indexing matches `se_devindex` (so slot 0 is the
  primary).
- `famfs_alloc_contiguous()` (`src/famfs_alloc.c:378-386`): take a
  `devindex` parameter; pass `lp->dev_bitmaps[devindex]->bitmap`,
  `->nbits`, `->cur_pos` into `bitmap_alloc_contiguous()`.
- Strided/interleaved path (`src/famfs_alloc.c:610, 666, 699-724`):
  every read of `lp->bitmap` / `lp->nbits` / `lp->cur_pos` is
  rewritten against `lp->dev_bitmaps[devindex]`.  There are seven
  sites total - easy to find with
  `grep -n 'lp->bitmap\|lp->nbits\|lp->cur_pos' src/famfs_alloc.c`.
- Lazy build path (`src/famfs_alloc.c:747-756`): currently builds a
  single bitmap on first use; rewrite to build the requested device's
  bitmap on first use (lazier and matches today's behavior for slot
  0; an "all up front" variant is fine too but not required).
- `famfs_build_bitmap()` declared at
  `src/famfs_lib_internal.h:77-81`: signature stays the same per
  call, but the caller now invokes it per device.  Today it takes
  `dev_size_in` directly - fine, the caller passes the per-device
  size from the daxdev table (Cross-cutting concerns; on the
  standalone path this init runs the Phase 4 walker over the log to
  build that table first).
- Free path in `famfs_release_locked_log()`
  (`src/famfs_lib.c:3520-3521`): free every bitmap in the array,
  then free the array.
- All call sites of `famfs_file_alloc_contiguous()` and the
  interleaved allocator default to `devindex=0` in this phase.  The
  device-selection policy (random + fallback) lands in Phase 10.

**Notes**: pure in-memory refactor; on-media format is untouched.
Depends on Phase 4 only for the whole-log accumulation walker, which
the standalone locked-log init runs over the log to learn how many
devices exist (on the fuse side `famfs_ctx.daxdev_table` is already
populated).  **Phase 6 is plumbing-only**:
every call site is updated to pass `devindex=0`, so behavior is
unchanged after this phase.  Verifiable with the existing `smoke/`
tests, which should produce identical bitmap stats output before and
after.

**Unit tests (same PR)**: `TEST(famfs, famfs_alloc)`
(`test/famfs_unit.cpp:466`) and `TEST(famfs, famfs_fmap_alloc_verify)`
(`:1597`) must continue to pass against `devindex=0` - this covers
the "behavior unchanged when the caller passes 0" requirement.  Add a
new `TEST(famfs, famfs_alloc_multi_dev_bitmap)` that constructs a
`famfs_locked_log` with N>1 `dev_bitmaps` slots, allocates from each
slot independently, and asserts the per-slot `cur_pos` advances
independently and the slots' bitmaps don't bleed into each other.

---

## Phase 7 - `FAMFSIOC_ADD_DAXDEV` / `GET_MAX_DAXDEV` ioctls

**Goal**: kernel entry points so the standalone (non-fuse) path can be
told about a new daxdev at runtime, **and** can ask the kernel which
daxdevs it already knows - so logplay does not re-register every device
on every replay.  Today logplay's only kernel-visible state is the
superblock(s) and the log; it has no way to know what is already
registered, so without this it would re-issue `ADD_DAXDEV` for every
secondary on each replay (not catastrophic, just wasted syscalls).

**Touchpoints**:

- `linux_include/linux/famfs_ioctl.h`: bump `FAMFS_KABI_VERSION` from
  43 to 44 (`:17`); add a "v3" ABI section after the existing v2 block
  (`:197-199`); allocate opcodes `0x56` and `0x57`:

  ```c
  #define FAMFSIOC_ADD_DAXDEV  _IOW(FAMFSIOC_MAGIC, 0x56, \
                                    struct famfs_ioc_add_daxdev)
  #define FAMFSIOC_GET_MAX_DAXDEV _IOR(FAMFSIOC_MAGIC, 0x57, __u32)
  ```

- New arg struct
  `struct famfs_ioc_add_daxdev { uuid_le uuid; __u64 size; __u32 index; char name[FAMFS_DEVNAME_LEN]; }`
  placed alongside the other ioctl arg structs (above
  `FAMFSIOC_MAGIC` at `linux_include/linux/famfs_ioctl.h:188`).
  `uuid` is the invariant; `name` is the locally-resolved path
  (Phase 3) that the kernel uses to bind to a backing device.
- **`ADD_DAXDEV` must be idempotent**: registering an already-known
  `(uuid, index)` is a **harmless no-op**, not an error.  This is what
  lets standalone logplay call it on every replay without coordination,
  and makes `GET_MAX_DAXDEV` a pure optimization (skip the redundant
  syscall) rather than a correctness requirement.
- `FAMFSIOC_GET_MAX_DAXDEV` returns the highest daxdev index the kernel
  currently knows (the primary is index 0, registered at mount; so a
  freshly mounted fs returns 0).  Indices are dense and assigned in log
  order, so "highest known index" is a sufficient resume point: the
  standalone logplay arm (Phase 4) queries this **once** at the start of
  replay and issues `ADD_DAXDEV` only for indices above it.
- Documented convention: invoke on the superblock-file FD or
  log-file FD (file types already enumerated at
  `linux_include/linux/famfs_ioctl.h:22-26`).
- **Shared wire struct with the fuse push delivery**: if the fuse daemon
  delivers daxdevs by *pushing* them (a separate, present-day delivery
  concern, not part of this roadmap), it issues this same
  `famfs_ioc_add_daxdev` wire struct against the fuse kernel side - the
  struct is shared; only the receiving kernel component differs.  That
  daemon does not need `GET_MAX_DAXDEV`: its lifetime equals the mount's,
  so it tracks pushed devices in memory.

**Notes**: kernel-side implementation lives outside this repo, but
the header must land here because the build prefers the bundled copy
when `/usr/include/linux/famfs_ioctl.h` is absent.

**Ordering with Phase 4 on the standalone path**: the kernel side of
this ioctl must accept any subsequent `FAMFSIOC_MAP_CREATE_V2` whose
extents carry `se_devindex=N` for the registered N.  The standalone
logplay arm from Phase 4 issues this ioctl *before* it materializes
`.meta/.superblock.N` (whose `famfs_v1_set_file_map()` call has
`se_devindex=N`), so the registration order on a standalone node is:
`ADD_DAXDEV` ioctl -> meta-file create -> subsequent log entries.
The fuse path has no equivalent ordering because shadow files are not
registered with the kernel.

**Unit tests (same PR)**: add `TEST(famfs, famfs_ioctl_abi_v3)` to
`test/famfs_unit.cpp` asserting `FAMFS_KABI_VERSION == 44`, that both
new ioctl numbers resolve to the expected `_IOW`/`_IOR` encodings
(`0x56` / `0x57`), and that `sizeof(struct famfs_ioc_add_daxdev)` plus
key `offsetof()`s match the documented ABI layout - these freeze the
wire format the kernel side will rely on.

---

## Phase 8 - daemon learns the daxdev table

**Goal**: `famfs_fused` becomes aware of every daxdev (not just index
0) by loading the per-device files logplay writes into its in-memory
`famfs_ctx.daxdev_table`.  This phase is purely table *population*; how
the daemon then hands daxdevs to the kernel is a separate delivery
concern that consumes the table (see "Delivery to the kernel is a
separate concern" below).  Primary is known at startup via the
`-o daxdev=` mount option (parsed at `src/famfs_fused.c:137-138`, stored
to `daxdev_table[0]` at `:1321-1327`).  Secondaries are recorded by
logplay as per-device files `<shadow>/daxdevs/N` (Phase 4); **this phase
makes the daemon load them** into the table.

**Mechanism**: logplay and the daemon are separate processes that share
the shadow tree, not memory (Cross-cutting concerns).  Logplay
maintains one file per device, `<shadow>/daxdevs/N` - above
`<shadow>/root`, each holding `{ uuid, size }` (devindex is the
filename), published atomically by temp+`rename()` and therefore
**lock-free**, log-derived.  The daemon loads them:

- **at startup**, after the existing slot-0 init from `-o daxdev=`:
  `readdir` `<shadow>/daxdevs/` and read each file into slots `1..N`
  (grow the `calloc` at `src/famfs_fused.c:1322-1325` to `max_daxdevs`);
- **lazily**, when the daemon first needs an index not yet in the
  in-memory table (e.g. after a `devadd` + logplay published a file):
  open `<shadow>/daxdevs/N`.

For each device the daemon resolves uuid -> devname via the Phase 3
resolver/cache (`<shadow>/daxdev_map.yaml`, also above `root/`,
host-local).  **The files store no devname** - that is host-local
and the daemon supplies it, so different hosts can disagree on the path
for the same uuid.

Why these files live above `root/` and not in `.meta`: they are the
logplay -> daemon **IPC channel** - daemon plumbing, not served
filesystem content - so they stay out of the mount.  (The actual
superblock *content* for each secondary is reachable as filesystem
content via `.meta/.superblock.N`, which is a separate thing.)
Per-device files above `root/` - rather than a single table file - are
what make the channel **lock-free**: one file per devindex N (the access
key, like `.meta/.superblock.N`) means there is no shared
read-modify-write to serialize, and atomic temp+`rename()` publish gives
readers an all-or-nothing view with no flock.  (The earlier objection to
per-index files - replication into the mount - applied only to
`.meta/daxdevs/N`, *inside* the served tree; `<shadow>/daxdevs/N` is
daemon-private above `root/`, exactly like the rest of this channel, so
it does not replicate.)  The cost is a `readdir` + N opens at startup
instead of one read - negligible for a handful of devices, and lazy
access opens a single file by index.

### Delivery to the kernel is a separate concern

Populating `daxdev_table` is all this phase does.  *How* the daemon then
hands a daxdev to the kernel - the kernel pulling `GET_DAXDEV` and the
daemon answering from the table, or the daemon pushing via
`fuse_daxdev_open` - is an orthogonal, **present-day** delivery mechanism
(it already exists for device 0, with no multi-daxdev machinery) that
simply *consumes* this table.  It is not introduced by multi-daxdev and
is not specified here; once the table holds more than slot 0, whichever
mechanism is in use picks up the new rows unchanged.

**Touchpoints**:

- Daemon loader: a thin `famfs_fused.c` routine that fills
  `famfs_ctx.daxdev_table` by calling the shared per-device
  `<shadow>/daxdevs/N` **parser in `famfs_dax.c`** (the same helper
  Phase 4's writer pairs with) - one file per device, no flock (atomic
  rename publish).  At startup it `readdir`s `<shadow>/daxdevs/`; on a
  lazy miss it opens a single file by index.  Phase 4 is the writer.

**Unit tests (same PR)**: add `TEST(famfs, famfs_daxdev_files)`.
Write known `<shadow>/daxdevs/1..N` fixtures, load them through the
daemon's loader helper, and assert `{ uuid, size }` round-trip per file
keyed by the filename index, with no devname (that comes from the
resolver).  Assert that loading an index with no file yet fails
gracefully, and that after publishing `<shadow>/daxdevs/N` (atomic
rename) a subsequent load finds it.  Cover at least one malformed file
to confirm a graceful error.

---

## Phase 9 - multi-device fsck

**Goal**: make `famfs fsck` work in a multi-daxdev world.

**This phase ships as two commits.**  The split keeps the first commit
free of any Phase 3 dependency - it only needs to *read* a superblock
and reject a secondary - while the second commit adds the
resolver-backed convenience of following a secondary to its primary.

### Commit 1 - multi-device fsck, primary required

Three changes, none of which need the Phase 3 resolver:

1. **Per-device capacity reporting**: overall used / free as today,
   plus a per-device breakdown gated on `verbose`.
2. **Primary-only input for unmounted fsck**: when fsck is invoked by
   daxdev path (as opposed to mount point or file path), the device
   must be the primary.  Raw-mmap the supplied device's superblock,
   check `ts_sb_flags` (Phase 2): if `FAMFS_SECONDARY_SB`, reject with
   a clear error - "this is a secondary device; fsck via the primary,
   or via the mount point"; if neither SB flag is set, reject as
   not-a-famfs-member.  No resolver, no redirect yet.
3. **Re-introduce the raw-mmap fsck path under `daxmode_required`**:
   today on kernel >= 7.0, unmounted fsck-by-device goes through a
   dummy mount (`src/famfs_lib.c:3163-3191`).  That can't even classify
   an arbitrary member device: a *secondary* has no log, so
   dummy-mounting it to read its superblock would fail or fabricate a
   log.  Raw-mmap the supplied device's superblock instead - which both
   the secondary-rejection above (commit 1) and the secondary-redirect
   below (commit 2) need - then raw-mmap the primary for the actual
   scan.  Switch the daxdev to `DAXDEV_MODE_DEVICE_DAX` and use raw
   mmap throughout.  Raw mmap does not work in famfs mode - devdax mode
   is mandatory for this path *until* the Cross-cutting "raw read/write
   on famfs-mode devices" enabler lands, at which point this path reads
   the superblock in place and the devdax flip + restore below can be
   dropped.

### Commit 2 - follow a secondary to its primary

Replace commit 1's secondary *rejection* with a *redirect*: accept any
member daxdev.  If the supplied device is a secondary
(`FAMFS_SECONDARY_SB`), read its `ts_primary_dev_uuid` (Phase 2) and
resolve it to the primary's local devname via the Phase 3 resolver,
then fsck *that* device.  fsck still only ever operates on the primary
(it's the one with the log); the secondary argument is just a
convenient handle.  Once the primary is open, the log scan
(`famfs_fsck_scan()`) discovers the remaining devices into the devlist
- the operator does not have to know which device is the primary.
This is the **only** part of Phase 9 that depends on Phase 3.

**Touchpoints**:

- `famfs_fsck()` (`src/famfs_lib.c:3098-3232`): the
  unmounted-by-device arm at `:3148-3215` becomes one path instead of
  two.
  - **Note**: do not reuse `famfs_check_or_set_daxmode()`
    (`src/famfs_dax.c:264`) for the devdax switch - it hard-codes the
    target to `DAXDEV_MODE_FAMFS` at `:304`.  Call
    `famfs_set_daxdev_mode(path, DAXDEV_MODE_DEVICE_DAX, verbose)`
    (`src/famfs_dax.c:154`) directly, gated on `set_daxmode` the way
    the existing famfs-mode switch is gated at
    `src/famfs_lib.c:3169`.
  - Capture `initial_daxmode` from `famfs_get_daxdev_mode()`
    (`src/famfs_dax.c:66`, also used at `src/famfs_lib.c:2089`) on
    entry and restore on exit.  Follow the restore pattern already
    used by `famfs_fsck()` - the only working restore in tree per
    `markdown/daxmode-analysis.md`.
  - Call `famfs_mmap_superblock_and_log_raw()`
    (`src/famfs_lib.c:855`) - already exists and is already used by
    the non-`daxmode_required` arm at `:3194`.
  - After mmap, check `sb->ts_sb_flags` (Phase 2).  If neither
    `FAMFS_PRIMARY_SB` nor `FAMFS_SECONDARY_SB` is set (not a famfs
    member), error out in both commits.
    - **Commit 1**: if `FAMFS_SECONDARY_SB`, reject with the "fsck via
      the primary" error and return.
    - **Commit 2**: if `FAMFS_SECONDARY_SB`, instead read
      `ts_primary_dev_uuid`, resolve it to the primary's devname via
      `famfs_resolve_daxdev_uuid()` (Phase 3), restore *this* device's
      daxmode, and restart the fsck against the resolved primary
      devname.  Guard against a resolver miss (primary not present on
      this host) with a clear "primary daxdev <uuid> not found on this
      host" message.
  - Delete or fence off the dummy-mount fsck arm (`:3174-3190`) - it
    becomes unnecessary once raw-mmap works under `daxmode_required`.
- `famfs_fsck_scan()` (`src/famfs_lib.c:666-820`): grow per-device
  reporting.
  - Today reads `sb->ts_daxdev.dd_size` once at `:696`.  Replace with
    a walk of all devices: primary from `sb->ts_daxdev`, secondaries
    discovered by scanning the log for `FAMFS_LOG_ADD_DAXDEV` entries
    (Phase 1) - this is the same accumulation Phase 4 does for the
    fuse daemon, factored into a helper that both can use.
  - **Per-device capacity (commit 1) needs no secondary-SB read.**
    `dd_size` for each device comes from the daxdev table (log-derived,
    via the `ADD_DAXDEV` entry), so the capacity breakdown has no Phase
    3 dependency.
  - **Reading and validating each secondary superblock** is the deeper
    check.  The log gives index N and `dd_uuid`; from there:
    - *mounted* (available in commit 1): open
      `<mount>/.meta/.superblock.N` directly (O(1) by index - the Phase
      4 naming decision) and mmap it.  No resolver needed - it's a file
      in the mount.
    - *unmounted by-device* (commit 2, since it needs the resolver to
      reach other devices): resolve `dd_uuid` -> devname (Phase 3) and
      raw-mmap that device's superblock.
    Either way, cross-check the superblock's `ts_dev_uuid` against the
    log entry's `dd_uuid` for index N, confirm `FAMFS_SECONDARY_SB`,
    and confirm `ts_primary_dev_uuid` matches the primary's
    `ts_dev_uuid`.  A mismatch is an fsck error.  This is what makes
    fsck actually *check* the secondaries rather than trust the log.
  - Build N bitmaps via N calls to `famfs_build_bitmap()`
    (`src/famfs_lib_internal.h:77-81`).  Phase 6 already introduces
    the per-device split for the allocator; reuse whatever
    filter-by-devindex extension Phase 6 lands on.
  - Existing capacity block (`:747-771`) prints overall totals as
    today (summed across devices).  Add a `verbose` block underneath
    that iterates devices and prints index, devname, capacity,
    allocated, free in the same `human` / non-`human` format pair.
  - Histogram path (`:782-786`) currently calls
    `famfs_fsck_bucket_info` on the single bitmap; in `verbose` mode,
    run it per device.
- `famfs_fsck_mounted()` (`src/famfs_lib.c:2957`): inherits the
  `famfs_fsck_scan()` improvements for free - no per-call changes
  needed.
- CLI help text (`src/famfs_cli.c:677-714`, `famfs_fsck_usage`):
  commit 1 documents the primary-only restriction for
  unmounted-by-device fsck; commit 2 updates it to say any member
  daxdev is accepted and a secondary is redirected to its primary.
  Both note the device(s) are switched to devdax mode for the
  duration.

**Notes**:

- Per-device output is `verbose`-gated, matching how the existing
  histogram is already `nbuckets`-gated (separate knob, same shape).
- Redirecting a secondary to its primary (rather than operating on the
  secondary in place) still leaves the door open for a follow-up that
  lets fsck validate a secondary *alone* - just its superblock and
  `ts_primary_dev_uuid`, without opening the primary.  Out of scope
  here.
- Only **commit 2** depends on Phase 3: the secondary->primary redirect
  needs `famfs_resolve_daxdev_uuid()`.  Commit 1 (primary-required)
  depends only on Phase 2 (read `ts_sb_flags` to reject a secondary).
  Reflected in the dependency shape.
- Reviving the raw-mmap arm under `daxmode_required` collapses two
  code paths into one and removes the fsck-specific dummy-mount call
  sites (`src/famfs_lib.c:3175, 3179`).  Remaining dummy-mount call
  sites (mkfs at `:5629, 5631`; `famfs_dax_shadow_logplay` at
  `:2126, 2130`) are unaffected.

**Unit tests**: each commit lands its own.

- **Commit 1**: add `TEST(famfs, famfs_fsck_scan_multi_dev)` that
  synthesizes an in-memory `famfs_superblock` plus log with N devices
  referenced by `se_devindex`, then drives `famfs_fsck_scan()` and
  asserts: overall capacity / allocated / free equal the sum across
  devices; per-device output is emitted when `verbose` is set and
  suppressed when not; existing single-device output is byte-identical
  to today's when N=1 (snapshot the current output and diff).  Add
  `TEST(famfs, famfs_fsck_rejects_secondary_device)`: the by-device
  fsck path returns an error and a clear message when handed a
  superblock with `FAMFS_SECONDARY_SB` set, and rejects a superblock
  with neither SB flag set as not-a-member.  Add `TEST(famfs,
  famfs_fsck_secondary_sb_validation)`: drive the *mounted* scan
  against a fixture with N secondaries whose `.meta/.superblock.N`
  files are consistent with the log, and assert it passes; then mutate
  one secondary's `ts_dev_uuid` to disagree with its `ADD_DAXDEV`
  entry, and a second case where a secondary's `ts_primary_dev_uuid`
  doesn't match the primary's `ts_dev_uuid`, and assert each is
  reported as an fsck error naming the offending index.
- **Commit 2**: replace `famfs_fsck_rejects_secondary_device` with
  `TEST(famfs, famfs_fsck_secondary_redirects_to_primary)`: hand the
  by-device fsck path a superblock with `FAMFS_SECONDARY_SB` set and a
  `ts_primary_dev_uuid`, with a seeded resolver cache mapping that uuid
  to a primary devname, and assert fsck redirects to (opens) the
  primary rather than erroring.  Add a companion case where the
  resolver misses and assert the clear "primary not found on this
  host" error.  The not-a-member rejection case carries over unchanged.
  Extend the secondary-SB validation to the *unmounted by-device* path:
  with the resolver mapping each secondary `dd_uuid` to a device whose
  raw superblock is mutated to mismatch, assert the same per-index fsck
  errors as the mounted case.

---

## Phase 10 - multi-device allocator policy

**Goal**: put the per-device bitmaps from Phase 6 to work.  Allocation
policy changes:

1. **Non-interleaved allocation** (the `famfs_file_alloc_contiguous()`
   path) picks a random daxdev from the current `daxdev_table`,
   tries the allocation against that device's bitmap, and falls back
   to other devices in random order on `-ENOMEM`.  Returns `-ENOMEM`
   only when every device has been tried.
2. **Interleaved allocation** (the `famfs_file_strided_alloc()` path)
   drops `nbuckets` entirely.  Each strip lands on a distinct daxdev,
   in random device order.  The current invariant `nstrips <=
   nbuckets` becomes `nstrips <= ndaxdevs`.

**Touchpoints**:

`struct famfs_interleave_param` (`src/famfs_lib.h:34-38`):

- Remove the `nbuckets` field (`:35`).  Update the doc comment so it
  no longer references bucketization.

CLI: drop `-B|--nbuckets` from every command that accepts it.  The
option-table call sites today are:

- `do_famfs_cli_fsck()` at `src/famfs_cli.c:718, 728, 765`.  This one
  is a histogram knob, **not** an allocator knob, and doesn't
  reference `interleave_param` - **keep it**.
- `do_famfs_cli_creat()` option table at `src/famfs_cli.c:888, 946`
  and help text at `:840-841`.
- `do_famfs_cli_flush()` option table at `src/famfs_cli.c:1732,
  1794` and help text at `:1441-1442`.
- `do_famfs_cli_cp()` - same pattern, drop the bucket option.
- YAML config: `famfs_emit_interleave_param_yaml()` and
  `famfs_parse_alloc_yaml()` (`src/famfs_lib.h:155, 158`) stop
  emitting `nbuckets`.  The parser
  (`src/famfs_yaml.c:1110-1121`) must accept old YAML that still has
  `nbuckets` and silently ignore it (with a `verbose`-gated warning),
  to keep existing `.meta/.alloc.cfg` files readable across the
  upgrade.

Non-interleaved alloc, `famfs_file_alloc_contiguous()`
(`src/famfs_alloc.c:408-441`) and its helper `famfs_alloc_contiguous()`
(`:378-386`):

- Replace the single `bitmap_alloc_contiguous(lp->bitmap, ...)` call
  with a loop:
  1. Generate a random permutation of `[0, lp->n_dev_bitmaps)` using
     the existing `bucket_series_alloc()` / `bucket_series_next()`
     helpers (`src/famfs_alloc.c:447-505`).  The helper is already
     "random permutation of N integers".
  2. For each devindex in the permutation, call
     `bitmap_alloc_contiguous()` against
     `lp->dev_bitmaps[devindex]`.  Stop on first success.
  3. On success, return an extent with the matching `se_devindex`
     (currently hard-coded to 0 at `src/famfs_alloc.c:432`).
- The fallback must update the *chosen* device's `cur_pos`, not
  globally - Phase 6's per-device `cur_pos` already isolates this.
  `bitmap_alloc_contiguous()` already writes through the `pos`
  pointer it's handed (today: `&lp->cur_pos` at
  `src/famfs_alloc.c:385`), so passing
  `&lp->dev_bitmaps[devindex]->cur_pos` per iteration is sufficient -
  no extra bookkeeping.

Interleaved alloc, `famfs_file_strided_alloc()`
(`src/famfs_alloc.c:579-727`):

- Drop the validation that `nstrips <= nbuckets` at `:603` and
  `src/famfs_alloc.c:546-552`.  Replace with `nstrips <= ndaxdevs`
  (sourced from `lp->n_dev_bitmaps`).
- Drop the `bucket_size_au`, `devsize_au / nbuckets` calculation at
  `:620-622, 631-636`.  There's no bucket geometry any more; each
  strip is contiguous on its assigned device, length
  `strip_size_au * alloc_unit`.
- The loop at `:660-686` currently iterates buckets in random order.
  Repurpose `bucket_series_alloc(bs, lp->n_dev_bitmaps, 0)` to
  randomize *devices* instead.  Each iteration:
  1. Picks the next random devindex.
  2. Calls
     `bitmap_alloc_contiguous(lp->dev_bitmaps[devindex]->bitmap, ..->nbits, ..., &lp->dev_bitmaps[devindex]->cur_pos, range_size=0)`
     - no range restriction (the whole device is the search space),
     and `cur_pos` is passed by pointer so
     `bitmap_alloc_contiguous()` advances the per-device high-water
     mark.  **This is the fix for today's bug** where the strided
     path uses a throwaway local `pos` at `src/famfs_alloc.c:663`
     and never writes back, leaving `cur_pos` stale or zeroed.
  3. On success, stamps the extent with `se_devindex = devindex`
     (not `0` as today at `:673`).
- After the loop, do **not** re-zero `cur_pos` (today's strided path
  does `lp->cur_pos = 0` at `:610` on the small-file fallback).
  Each per-device `cur_pos` is now an authoritative high-water mark;
  a subsequent contiguous alloc on the same device in the same
  locked-log session should resume from there.
- The "fewer strips than needed" failure path (`:688-715`) stays,
  but the free-back call (`:702-706`) must call
  `bitmap_free_contiguous()` against the right per-device bitmap, not
  a single global one.
- The small-file fallback at `:605-612` (file size < chunk_size ->
  contiguous alloc) inherits Phase 10's new random+fallback
  contiguous path for free.

Validation, `famfs_validate_interleave_param()`
(`src/famfs_alloc.c:512-577`):

- The function needs access to `ndaxdevs` to validate
  `nstrips <= ndaxdevs`.  Either thread `lp` through (caller already
  has it), or pass `ndaxdevs` directly.
- Drop the `bucket_size < 1G` check at `:568-574` - no more buckets.
- Keep the `chunk_size` power-of-2 and alloc-unit alignment checks
  (`:535-544`).

Helpers:

- `bucket_series_alloc()` / `bucket_series_next()` /
  `bucket_series_destroy()` / `bucket_series_rewind()`
  (`src/famfs_alloc.c:447-505`, declared at
  `src/famfs_lib_internal.h:94-97`) are name-misleading but
  functionally correct as a "random permutation of N integers"
  helper.  Leave the names; document that they're used for device
  permutations post-Phase-10.  A rename is a separate churny commit.

**Notes**:

- "Random device" is implemented via `bucket_series_alloc()` which
  uses Fisher-Yates with `rand()` (`src/famfs_alloc.c:486`) - match
  that, don't introduce a new RNG.
- The on-media format does not change: extent records have always
  had `se_devindex`, and Phase 1's log entries are unaffected.
- The fall-back order for non-interleaved alloc is "remaining
  devices in random order" - not "least-used" or "primary-first".
- Interleaved alloc currently requires `nstrips == nbuckets` in some
  smoke tests; those tests need updating to drop `--nbuckets` and
  set `--nstrips` only.

**Unit tests (same PR)**:

- Existing `TEST(famfs, famfs_alloc)` (`test/famfs_unit.cpp:466`)
  and `TEST(famfs, famfs_fmap_alloc_verify)` (`:1597`): update to
  seed a multi-device `daxdev_table`.  Single-device tables still
  work - the contiguous path with N=1 should be byte-identical to
  today's behavior, satisfying the "no regression" criterion.
- New `TEST(famfs, famfs_alloc_contiguous_multi_dev)`: seed N>1
  devices, force device 0 to be full, assert the allocator falls
  back to device 1 and returns an extent with `se_devindex=1`.
  Then force devices 0 and 1 full, assert it falls back to device 2.
  Finally, with all devices full, assert `-ENOMEM`.
- New `TEST(famfs, famfs_alloc_interleaved_multi_dev)`: seed N=4
  devices, request `nstrips=4`, assert each strip lands on a
  distinct device.  Assert `nstrips > ndaxdevs` is rejected by
  validation.  Run with `nstrips=3` against `ndaxdevs=4` and assert
  the chosen device subset is a random 3-of-4.
- New `TEST(famfs, famfs_alloc_yaml_backcompat)`: parse a YAML
  fixture that still includes a `nbuckets:` line and assert the
  parse succeeds, ignoring (or warning about) the obsolete field.
- New `TEST(famfs, famfs_alloc_cur_pos_monotonic)`: under a single
  `famfs_locked_log` session, do (a) an interleaved alloc that lands
  strips on devices 0/1/2, then (b) a contiguous alloc and assert it
  starts at or past each device's post-interleaved `cur_pos`, then
  (c) a second contiguous alloc on the same device and assert it
  starts at or past the first one's end.  This locks in the "famfs
  doesn't free, so `cur_pos` is monotonic across mixed alloc
  patterns" invariant the rewrite is supposed to establish.

---

## Verification

**Requirement: every phase (and every commit within a phase) must be
independently testable at its own boundary, and ships its tests in the
same PR as the functional change.**  Tests are not a follow-up.  If a
step's change cannot be verified without code from a later phase, the
step is mis-scoped - re-split it (as Phase 1's pure helper, Phase 6's
parity refactor, and Phase 9's two commits already do) until each
landable unit is testable on its own.

- Unit tests are added to `test/famfs_unit.cpp` as new
  `TEST(famfs, ...)` cases.  Each phase above names a specific test
  case to add or extend; the implementer is expected to land that
  test, not defer it.
- **Phase boundaries were chosen along testability seams.**  The seam
  that makes each step verifiable in isolation:
  - Phase 1 - the entry through `__famfs_add_daxdev()` into a mocked log
    (real CRC via the append path), plus `famfs_daxdev_table_add()` over
    hand-built entries (no logplay, resolver, or shadow tree).
  - Phase 2 - in-memory superblock construction + `famfs_check_super()`.
  - Phase 3 - seeded cache file + a mock `/sys` tree (or a `/sys`-bypass
    entrypoint); the cache logic is unit-tested, the real scan is not.
  - Phase 4 - the whole-log walker over a synthetic log buffer (pure,
    direct), plus the `shadow=1` logplay integration.
  - Phase 5 - the factored superblock-write helper against an in-memory
    buffer.
  - Phase 6 - per-device bitmaps in isolation, plus parity of the
    existing alloc tests at `devindex=0`.
  - Phase 7 - the ABI encoding of both ioctls
    (`_IOW`/`_IOR`/`sizeof`/`offsetof`) is frozen by a unit test even
    though the kernel behavior is not in this repo.
  - Phase 8 - `<shadow>/daxdevs/N` per-device load round-trip from
    fixtures (uuid/size keyed by filename index; open-on-missing-index).
  - Phase 9 - commit 1 (`famfs_fsck_scan()` over a synthetic
    superblock+log, mounted SB validation) is Phase-3-free; commit 2
    adds the resolver-backed redirect and unmounted validation.
  - Phase 10 - per-device contiguous fallback, distinct-device
    interleave, YAML back-compat, and `cur_pos` monotonicity, each
    against a seeded multi-device table.
- **Surfaces that necessarily fall to smoke tests, not unit tests** -
  these are the accepted boundary, not a coverage failure: kernel-side
  ioctl *behavior* (Phase 7; only the wire format is unit-tested), the
  *standalone* meta-file registration path (Phase 4, gated on Phase 7's
  ioctl), real `/sys/bus/dax` scanning (Phase 3), and end-to-end CLI /
  fuse behavior (Phases 5, 7, 8, 9, 10).  Smoke tests live under
  `smoke/` (driven by `run_smoke.sh` per the `Makefile`).
- Existing tests (`TEST(famfs, famfs_alloc)`, `TEST(famfs,
  famfs_super_test)`, `TEST(famfs, famfs_log)`, etc.) must
  continue to pass.  For phases that refactor without changing
  behavior (Phase 6 in the `devindex=0` case, Phase 9 for
  single-device fsck, Phase 10 for the single-daxdev case where
  random-device-of-1 collapses to today's behavior), parity with
  current test output is itself an acceptance criterion.

Cross-link this roadmap to `markdown/famfs-fuse-design-notes.md`
(which will need updates alongside Phase 8) and
`markdown/famfs-cli-reference.md` (Phase 5).

---

## Out of scope

- Concrete kernel-side ioctl implementation
- Kernel-side implementation of the "raw read/write on famfs-mode devices"
  enabler (Cross-cutting concerns).  It is tracked here as a dependency
  that simplifies Phases 3 and 9; the userspace in this roadmap is
  designed to work without it (via the devdax-flip + restore
  discipline) and to simplify once it lands.
- UUID-on-the-command-line input for `mkfs.famfs` / `devadd` (Phase 5
  takes devnames only; `--uuid` is a follow-up over the Phase 3
  resolver).
- A coded DCD-era resolver branch.  The DCD sysfs uuid interface is
  not settled; Phase 3 reads superblocks manually for now and swaps in
  the sysfs read later behind the same entry points.
- Failure/recovery semantics when a secondary daxdev is missing at
  mount
- Renaming `bucket_series_*` helpers to reflect their new role as a
  generic random-permutation helper
- Allowing fsck to operate on a secondary device alone (validate its
  superblock and `ts_primary_dev_uuid` only)
- Daxdev **delivery to the kernel** (the daemon answering `GET_DAXDEV`
  pulls, or pushing via `fuse_daxdev_open`) - a separate, present-day
  concern that consumes the `daxdev_table`, not part of this roadmap
- Allocation policies beyond "random device + fallback" / "stripe
  across distinct devices" (e.g. least-used, primary-first,
  caller-specified)
