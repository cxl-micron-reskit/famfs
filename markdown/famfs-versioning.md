<p align="center">
  <img src="famfs-logo.svg" alt="famfs logo">
</p>

# famfs Versioning and the "v1 / v2" Terminology

The label "v1 / v2" has been attached to famfs since its early days, but over time
it got stuck to *three different things* that do not line up with each other. This
document defines each one, explains why they are independent, and recommends
terminology to use going forward so the ambiguity stops spreading.

Short version: there are two famfs **implementations** (standalone and fuse), and
the standalone implementation has two **ioctl ABI** revisions (KABI 42 and 43).
The version numbers on those two axes are unrelated.

## The three axes

### Axis 1 - Implementation / architecture: standalone vs fuse

This is the big one. famfs exists in two implementations, represented by
`enum famfs_type` (`src/famfs_lib.h`):

```c
enum famfs_type {
	FAMFS_MODE_UNSET = -1, /* no explicit mode chosen yet */
	NOT_FAMFS = 0,         /* kernel has no famfs support */
	FAMFS_V1,              /* standalone famfs kernel module */
	FAMFS_FUSE,            /* FUSE-based famfs (famfs_fused daemon) */
};
```

- **Standalone** (`FAMFS_V1`) is the original famfs, introduced in 2024 as a
  complete in-kernel filesystem living in its own kernel module. Userspace pushes
  all file-to-dax metadata into the kernel via the `FAMFSIOC_*` ioctls when the
  log is played.

- **Fuse** (`FAMFS_FUSE`) is the later reimplementation on top of FUSE. The kernel
  side shrinks to a thin famfs iomap hook inside the stock `fuse` module (symbol
  `famfs_fuse_iomap_begin`); the filesystem logic moves into the `famfs_fused`
  userspace daemon; and metadata is served lazily via the shadow tree plus the
  fuse `GET_FMAP` path at lookup time, rather than being pushed in via ioctls.

Note there is **no `FAMFS_V2` symbol** - the successor to "v1" on this axis is
literally named `FAMFS_FUSE`. Some kernel *branch* names (e.g. `famfs-fuse-v2`)
do call the fuse work "v2", which is where the informal "v2 == fuse" association
comes from. In the userspace code, the `_v1` suffix almost always refers to *this*
axis meaning "standalone": `famfs_dummy_mount_v1()` vs `famfs_dummy_mount()`
(fuse), `file_is_famfs_v1()`, and `FAMFS_MODE=v1`.

### Axis 2 - Standalone kernel module name: `famfs` vs `famfsv1`

The standalone module is detected under two possible names
(`src/famfs_misc.c:famfs_get_kernel_type()`, `src/famfs_lib.c`):

- On standalone-only kernels the module is simply **`famfs`**.
- On kernels that carry *both* implementations, the standalone module was renamed
  **`famfsv1`** so that the fuse path could own the `famfs` name and hooks.

The "v1" in `famfsv1` means "the first-generation (standalone) module." It is
**not** a statement about the ioctl ABI: a current `famfsv1` module speaks the
*v2* (KABI 43) ioctl ABI described below.

### Axis 3 - Standalone ioctl ABI (KABI): v1 = 42, v2 = 43

This axis lives entirely *inside* standalone mode and describes the ioctl
interface between userspace and the standalone kernel module. It is versioned by
`FAMFS_KABI_VERSION` in the uapi header `linux/famfs_ioctl.h`, which the header
itself annotates as "aka v1" (42) and "aka v2" (43):

| Opcode | Name | ABI | Notes |
|--------|------|-----|-------|
| 0x50 | `FAMFSIOC_MAP_CREATE`    | v1 / KABI 42 | Create a map, **simple extents only** (`famfs_ioc_map`) |
| 0x51 | `FAMFSIOC_MAP_GET`       | v1 / KABI 42 | Get map header (size + extent count) |
| 0x52 | `FAMFSIOC_MAP_GETEXT`   | v1 / KABI 42 | Get the extent list (second call after GET) |
| 0x53 | `FAMFSIOC_NOP`          | v1 / KABI 42 | "Is this file in famfs?" probe (no payload) |
| 0x54 | `FAMFSIOC_MAP_CREATE_V2` | v2 / KABI 43 | Create a map, **simple + interleaved/striped** (`famfs_ioc_fmap`) |
| 0x55 | `FAMFSIOC_MAP_GET_V2`   | v2 / KABI 43 | Get the whole map in a single call (`famfs_ioc_get_fmap`) |

Relative to v1, the **v2** ioctl ABI adds interleaved/striped extents, a
`devindex` field for multi-device filesystems, and a single-shot "get" (v1
requires `MAP_GET` followed by `MAP_GETEXT`).

A third revision, **KABI 44 (v3)**, is planned: it replaces the v1/v2 map
ioctls with a single self-describing, extensible fmap format shared with the
fuse `GET_FMAP` path, and adds a standalone daxdev-registration ioctl. See
`markdown/kabi-44.md`.

Userspace selects between v1 and v2 ioctls at **compile time**, not by
negotiating with the running kernel. Every call site is written as:

```c
if (FAMFS_KABI_VERSION > 42) {
#if (FAMFS_KABI_VERSION > 42)
	... v2 ioctl ...
#endif
} else {
	... v1 ioctl ...
}
```

`FAMFS_KABI_VERSION` comes from whichever `famfs_ioctl.h` the binary was built
against: `CMakeLists.txt` prefers the running kernel's
`/usr/include/linux/famfs_ioctl.h` and falls back to the bundled
`linux_include/` copy (currently KABI 43). So the binary is hardwired to the ABI
of its build-time header.

## Why this is confusing

The same "v1 / v2" token means three unrelated things:

| Token in the code | Axis | What it actually means |
|-------------------|------|------------------------|
| `FAMFS_V1`, `_v1()` fn suffix, `FAMFS_MODE=v1` | implementation | **standalone** (as opposed to fuse) |
| `famfsv1` (module name) | module | the **standalone module**, generation 1 (speaks KABI 43) |
| "v1"/"v2" in `famfs_ioctl.h`, `FAMFS_KABI_VERSION`, `_V2` opcodes | ioctl ABI | **KABI 42 vs KABI 43** |

The sharpest example of the mismatch: the `famfsv1` module (module generation 1)
implements the **v2** ioctl ABI (KABI 43) for `FAMFS_V1` (standalone)
mode - three "v1/v2"s, none of them referring to the same version of the same
thing.

## Recommended terminology going forward

To stop the ambiguity from spreading, reserve "v1 / v2" for at most one axis and
name the others explicitly:

1. **Implementation:** say **"standalone" vs "fuse"** (the documentation already
   leans this way). Avoid "famfs v2" as a synonym for fuse. Prefer `standalone`
   over the `_v1` suffix / `FAMFS_MODE=v1`; keep `FAMFS_V1` only as a legacy enum
   alias.
2. **Ioctl ABI:** say **"KABI 42 / KABI 43"** (or "fmap v1 / v2"), never bare
   "v1 / v2", and keep it explicitly scoped to standalone.
3. **Module name:** treat `famfsv1` as just an opaque module name meaning "the
   standalone module," remembering that its "v1" is the architecture generation,
   unrelated to KABI.

Canonical one-liner:

> famfs has two implementations: **standalone** (the original 2024 in-kernel
> filesystem, module `famfs`/`famfsv1`, metadata pushed via `FAMFSIOC_*` ioctls)
> and **fuse** (the `famfs_fused` daemon plus a famfs iomap hook in the `fuse`
> module, metadata served via the shadow tree / `GET_FMAP`). Separately, the
> standalone **ioctl ABI** has two revisions: **KABI 42** (v1: simple extents)
> and **KABI 43** (v2: adds interleaved/striped extents and multi-device). These
> two "v1/v2" numbers are independent.

## See also

- `src/famfs_lib.h` - `enum famfs_type`
- `src/famfs_misc.c` - `famfs_get_kernel_type()`, `famfs_select_mode()`
- `linux/famfs_ioctl.h` (bundled at `linux_include/linux/famfs_ioctl.h`) - the
  KABI 42/43 ioctl definitions
- `markdown/famfs-fuse-design-notes.md` - how the fuse implementation works
- `markdown/fuse-vs-standalone-control.md` - choosing an implementation at runtime
