<p align="center">
  <img src="famfs-logo.svg" alt="famfs logo">
</p>

# KABI 44: Third-Generation Self-Describing fmap + Standalone Daxdev Registration

## Status

Design / roadmap. Not yet implemented. Spans two repos: the famfs userspace
(this repo) and the standalone famfs kernel module (out of tree; the uapi
header `linux/famfs_ioctl.h` lands here because the build prefers the bundled
copy in `linux_include/` when the system header is absent).

Terminology in this document follows `markdown/famfs-versioning.md`: "standalone"
vs "fuse" for the two implementations, and "KABI 42/43/44" for the standalone
ioctl ABI revisions. "v1/v2/v3" here always means the ioctl ABI (KABI
42/43/44), never the implementation.

## Motivation

The standalone ioctl ABI carries file maps in bespoke structs that have been
revised once already:

- **KABI 42 (v1)**: `struct famfs_ioc_map`, simple extents only, single device,
  two-step get (`MAP_GET` then `MAP_GETEXT`).
- **KABI 43 (v2)**: `struct famfs_ioc_fmap` / `famfs_ioc_get_fmap`, adds
  interleaved/striped extents and `devindex`, one-shot get.

Every extent-format change so far has meant a new struct and a new pair of
ioctl opcodes. Meanwhile the **fuse** side already has a self-describing,
extensible fmap wire format used for `GET_FMAP` replies -
`struct fuse_famfs_fmap_header` (`libfuse/include/fuse_kernel.h`):

```c
struct fuse_famfs_fmap_header {
	uint8_t  file_type;     /* enum fuse_famfs_file_type */
	uint8_t  reserved;
	uint16_t fmap_version;  /* FAMFS_FMAP_VERSION */
	uint32_t ext_type;      /* enum famfs_ext_type: SIMPLE | INTERLEAVE */
	uint32_t nextents;
	uint32_t fmap_size;     /* total message bytes: header + all extents */
	uint64_t file_size;
	uint64_t reserved1;
};
/* followed by either:
 *   SIMPLE:     struct fuse_famfs_simple_ext [nextents]
 *   INTERLEAVE: for each of nextents: struct fuse_famfs_iext, then its
 *               ie_nstrips * struct fuse_famfs_simple_ext strip list
 */
```

Two properties make this format future-proof:

1. **`fmap_size`** - the message is self-delimiting. A reader can size its
   buffer from the header alone (this is exactly how the fuse `GET_FMAP` path
   handles an undersized reply buffer: reply with just the header, the reader
   reallocs to `fmap_size` and retries).
2. **`fmap_version` + reserved fields** - the extent encoding can evolve
   *in band*. Future format changes may not require a KABI bump at all; a
   reader negotiates on `fmap_version` instead.

**Goal**: make standalone famfs speak this same format over `MAP_CREATE` and
`MAP_GET`, retire the v1/v2 map structs, and share one serializer/parser
(`src/famfs_fmap.c`) between the fuse `GET_FMAP` path and the standalone ioctl
path. Separately, give standalone an ioctl to register daxdevs at an index (the
analog of the fuse `DAXDEV_OPEN` push), so multi-device fmaps
(`se_devindex > 0`) resolve on the standalone path too.

## End state at KABI 44

KABI 44 is the **next standalone famfs patch set**, and it is a clean break: it
drops as many legacy ioctls as it can (certainly all of the v1 and v2 map
stuff) and does not carry backward-compatible aliases. Because the plain
`MAP_CREATE` / `MAP_GET` names (and their opcode numbers) are freed by dropping
the old structs, the new fmap ioctls **reclaim those names** rather than
carrying a `_V3` suffix. "v3" describes the ABI generation in prose; the symbol
is just `FAMFSIOC_MAP_CREATE`.

`FAMFS_KABI_VERSION` = 44. The header defines exactly:

| Opcode | Name | Purpose |
|--------|------|---------|
| 0x50 | `FAMFSIOC_MAP_CREATE`      | create a file map; payload is the `fuse_famfs_fmap_header` format (reclaimed name + opcode) |
| 0x51 | `FAMFSIOC_MAP_GET`         | get a file map; same format, reply self-sized via `fmap_size` (reclaimed name + opcode) |
| 0x53 | `FAMFSIOC_NOP`             | **kept** - "is this file in famfs?" probe (no payload) |
| 0x56 | `FAMFSIOC_ADD_DAXDEV`      | register a daxdev at an index (standalone analog of fuse `DAXDEV_OPEN`) |
| 0x57 | `FAMFSIOC_GET_MAX_DAXDEV`  | highest daxdev index the kernel knows (logplay resume point) |

**Not defined at all**: the v1 structs (`famfs_ioc_map`, `famfs_extent`), the v2
structs (`famfs_ioc_fmap`, `famfs_ioc_get_fmap`), `MAP_GETEXT` (0x52),
`MAP_CREATE_V2` (0x54), and `MAP_GET_V2` (0x55). A KABI-44 header carries none of
these opcodes or structs.

Opcodes 0x50 and 0x51 are **reused** with the new self-describing struct. This
is safe: the `_IOW`/`_IOR` encoding folds `sizeof(struct)` into the ioctl
number, so the new `FAMFSIOC_MAP_CREATE` is a *different* 32-bit ioctl constant
from the old one, and a version-mismatched caller gets `-ENOTTY` rather than a
silent misinterpretation. (See Open decision 5 if you would rather burn fresh
opcode numbers instead of reclaiming 0x50/0x51.) Opcodes 0x56/0x57 are the ones
already reserved for KABI 44 by `markdown/multi-daxdev-roadmap.md` Phase 7.

## Why the old ioctls can go

From the current userspace audit, the live callers of the old opcodes are:

- `MAP_CREATE` (0x50): `mkmeta` (superblock + log), `famfs clone`
- `MAP_GET` (0x51): `file_has_v1_map` (mkmeta probe), `famfs clone`, `famfs check`
- `MAP_GETEXT` (0x52): `famfs clone`
- `MAP_CREATE_V2` (0x54): regular file create
- `MAP_GET_V2` (0x55): `famfs getmap`
- `NOP` (0x53): membership probe - **kept**

Phases D and E below re-point every one of these at the reclaimed
`FAMFSIOC_MAP_CREATE` / `FAMFSIOC_MAP_GET` (v3 format). `NOP` has no replacement
and stays.

Because the new ioctls **reclaim the `MAP_CREATE`/`MAP_GET` names**, the header
cannot define an old and a new `FAMFSIOC_MAP_CREATE` simultaneously. So the
header flip (Phase A), the caller conversions (Phases D/E), and the deletion of
the old code (Phase F) are **one atomic change** on the userspace side, not
separately shippable increments - which is fine, since a clean-break KABI 44
needs a new standalone kernel to test against anyway. Only the KABI-agnostic
codec refactor (Phase B) can land on its own ahead of time.

## Phases

Ordered so the tree always builds and each phase is independently reviewable.

### Phase A - freeze the KABI 44 header + shared fmap struct
- Bump `FAMFS_KABI_VERSION` 43 -> 44 in `linux/famfs_ioctl.h`.
- Add the v3 opcodes and the fmap header struct (see Open decision 4 on whether
  that struct is shared from `fuse_kernel.h` or a layout-identical twin).
- Unit test freezes the contract: opcode encodings (`_IOW`/`_IOR`), the KABI
  constant, `sizeof`/`offsetof` of the fmap header and extent structs, and
  `fmap_msg_min_size()`.
- Kernel-side implementation is out of tree, but the header lands here.

### Phase B - shared userspace fmap codec
- Generalize the serializer/parser in `src/famfs_fmap.c`
  (`famfs_log_file_meta_to_msg()` and friends) so they are not
  fuse-`GET_FMAP`-specific: one emitter (`log_fmap` -> wire) and one parser
  (wire -> in-kernel call), usable by both `famfs_fused` and the standalone
  `MAP_CREATE` (v3) path.
- Pure refactor, no behavior change; gated on the existing fuse smoke tests.

### Phase C - `FAMFSIOC_ADD_DAXDEV` / `GET_MAX_DAXDEV`
- Implement the daxdev registration ioctl for standalone (see Open decision 1
  for the arg form). Primary is index 0, registered at mount as today.
- `ADD_DAXDEV` is idempotent: re-registering a known `(id, index)` is a no-op,
  so logplay can call it on every replay. `GET_MAX_DAXDEV` is a pure
  optimization (skip already-registered indices), not a correctness
  requirement.
- This is the same work as `multi-daxdev-roadmap.md` Phase 7; that phase should
  be rebased onto this document's opcode assignments and (per Open decision 1)
  its arg struct.

### Phase D - `MAP_CREATE` (v3 format)
- Replace `famfs_v1_set_file_map()` / `famfs_v2_set_file_map()` with a single
  emitter built on the Phase-B codec that issues the reclaimed
  `FAMFSIOC_MAP_CREATE`.
- Every unconditional v1 `MAP_CREATE` caller moves over: `mkmeta` (superblock +
  log) with a single simple extent, and `famfs clone`. Combined with the
  already-dead v1 file-create fallback, nothing issues the old v1/v2 create
  opcodes afterward.
- Striped-file wire form is Open decision 2 (native interleaved vs unroll).

### Phase E - `MAP_GET` (v3 format)
- Replace the `famfs getmap` `MAP_GET_V2` path with the reclaimed
  `FAMFSIOC_MAP_GET`; the reply is a single self-sized message (`fmap_size`),
  reusing the fuse "header tells you the size, realloc, retry" protocol.
- Re-point the unconditional v1 get callers - `file_has_v1_map` (mkmeta probe),
  `famfs clone`, and `famfs check` - off `MAP_GET`/`MAP_GETEXT`.

### Phase F - delete the old ABI
- Drop the v1/v2 structs (`famfs_ioc_map`, `famfs_extent`, `famfs_ioc_fmap`,
  `famfs_ioc_get_fmap`), the `MAP_GETEXT` (0x52), `MAP_CREATE_V2` (0x54), and
  `MAP_GET_V2` (0x55) opcodes from `linux/famfs_ioctl.h`, and delete the
  `#if (FAMFS_KABI_VERSION > 42)` v1/v2 branches and the `famfs_v1_set_file_map`
  / `famfs_v2_set_file_map` helpers in the userspace.
- Opcodes 0x50/0x51 are **kept but redefined** to the v3 struct (name
  reclamation), not dropped. Keep `NOP` (0x53).
- Because the names are reclaimed, this is not a separable trailing step: it is
  part of the single atomic A+D+E+F change described above.

### Phase G - verification
- Standalone smoke on a KABI-44 kernel.
- Parity test: a file written via the v3 `MAP_CREATE` and read back via the v3
  `MAP_GET` round-trips byte-identically to the fuse `GET_FMAP` reply for the
  same on-media fmap (proves the shared codec really is shared).
- Confirm the clean-break behavior: a KABI-44 build refuses (or is not built
  against) a KABI-43 standalone kernel, with no silent misinterpretation.

## Interaction with the multi-daxdev roadmap

`markdown/multi-daxdev-roadmap.md` Phase 7 currently reserves KABI 44 with
`FAMFSIOC_ADD_DAXDEV` (0x56) and `FAMFSIOC_GET_MAX_DAXDEV` (0x57), and its
Phase 4 assumes the kernel accepts `MAP_CREATE_V2` extents carrying
`se_devindex=N`. Under this document that map-create becomes the reclaimed
`FAMFSIOC_MAP_CREATE` (v3 format) - `MAP_CREATE_V2` no longer exists. If both
efforts proceed, Phase 7's opcode reservations stay as-is and its Phase-4
ordering ("register daxdev N before materializing a map that references N") is
rebased onto the v3 `MAP_CREATE`.

## Open decisions

Each is flagged with a recommended option (**R**). None block writing the rest
of the design, but they change specific structs/flows.

1. **How `FAMFSIOC_ADD_DAXDEV` identifies the daxdev.**
   - (a) **fd + index** - userspace opens `/dev/daxN` and passes the fd; the
     kernel derives the `dax_device` from it. Mirrors fuse
     `DAXDEV_OPEN` (`fuse_backing_map{fd, daxdev_index}`); no in-kernel path
     resolution.
   - (b) **uuid/name + index** - pass `{uuid, size, index, name}` as the
     multi-daxdev Phase 7 draft specifies; the kernel binds by name and keeps
     the log-invariant uuid in the wire struct.
   - (c) **both** - fd for binding plus uuid to validate the fd is the expected
     device.
   - **R: (c).** fd matches the fuse push path and avoids kernel path
     resolution; carrying the uuid lets the kernel reject a mismatched device
     and keeps the invariant identity in the ABI. Also decide which fd the
     ioctl is issued on (the multi-daxdev draft says the superblock/log meta-file
     FD; a control-device is the alternative).

2. **Wire form `MAP_CREATE` (v3) accepts for striped files.**
   - (a) **native interleaved** - the kernel accepts `fuse_famfs_iext`+strips
     directly and does the chunk->strip offset math itself (as the fuse iomap
     path already does). Compact messages; no giant extent-count cap.
   - (b) **simple-extent only** - userspace unrolls a striped file to one
     simple extent per chunk before the ioctl, as the fuse simple-only path
     does today (this is why `FUSE_FAMFS_MAX_EXTENTS` is 524288).
   - **R: (a).** A fresh KABI-44 kernel has no reason to inherit the unroll
     limit, and native interleave keeps the ioctl messages small.

3. **Runtime-compatibility story for a KABI-44 userspace build.**
   **DECIDED: (a) clean break.** KABI 44 is the next standalone patch set and
   drops the legacy ioctls outright, so a KABI-44 build talks only to KABI-44
   kernels for standalone; there is no runtime fallback to 43. Consistent with
   the compile-time `FAMFS_KABI_VERSION` model and with standalone being on the
   deprecation path in favor of fuse. (Rejected: keeping the v2 ioctls compiled
   in for a runtime probe - contradicts dropping the old ioctls, more code.)

4. **Where the fmap header struct lives for the ioctl ABI.**
   - (a) **reuse `fuse_famfs_fmap_header`** - `famfs_ioctl.h` shares the exact
     struct from `fuse_kernel.h`. One definition; the wire format cannot drift
     between fuse and standalone.
   - (b) **twin `famfs_ioc_fmap_header`** - a layout-identical struct in
     `famfs_ioctl.h`; decouples the two uapi headers at the cost of keeping the
     layout in sync.
   - **R: (a)** if the kernel build can include both headers cleanly; otherwise
     **(b)** with a `static_assert`/unit-test lock on identical
     `sizeof`/`offsetof`.

5. **Reclaim opcodes 0x50/0x51, or burn fresh numbers.**
   - (a) **reclaim 0x50/0x51** - `FAMFSIOC_MAP_CREATE`/`MAP_GET` reuse their
     original opcode numbers with the new struct. The `_IOW`/`_IOR` size field
     makes the resulting 32-bit ioctl constant differ from the old one, so a
     mismatched caller gets `-ENOTTY`, not a misread.
   - (b) **fresh numbers** (e.g. 0x58/0x59) - avoids a reader of an strace/ioctl
     trace seeing "0x50" mean different structs on a 42/43 vs 44 kernel, at the
     cost of never reusing the clean numbers.
   - **R: (a).** Reclaiming the name and the number together is the cleanest end
     state, and the `_IOC` size guard removes the aliasing hazard. Only pick (b)
     if cross-version trace clarity matters more than tidy numbering.

## See also

- `markdown/famfs-versioning.md` - the v1/v2/v3 and standalone/fuse terminology
- `markdown/multi-daxdev-roadmap.md` - Phase 7 (`ADD_DAXDEV`/`GET_MAX_DAXDEV`)
  and the multi-device context
- `markdown/fuse-extents.md` - the fuse fmap/extent handling this reuses
- `linux/famfs_ioctl.h` (bundled at `linux_include/linux/famfs_ioctl.h`) - the
  current KABI 42/43 ioctl definitions
- `libfuse/include/fuse_kernel.h` - `fuse_famfs_fmap_header` and the fuse
  `GET_FMAP` / `DAXDEV_OPEN` definitions
