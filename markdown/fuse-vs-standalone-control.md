# famfs Mode Selection & Module Loading

## Background

famfs can mount via two mechanisms:

- **FUSE mode** (`FAMFS_FUSE`): the `famfs_fused` daemon provides the filesystem via
  the kernel FUSE layer.  DAX passthrough requires the `fsdev_dax` driver to be bound to
  the dax device (kernel >= 6.15).
- **V1 / standalone mode** (`FAMFS_V1`): the `famfsv1` (or `famfs`) kernel module
  provides the filesystem directly.  Available on kernels that carry the standalone module.

Once a filesystem is mounted, the CLI detects which kind it is by inspecting the mount
and behaves accordingly.  The interesting question is how the code decides *which mode to
use* for the commands that perform a mount: `mkfs.famfs`, `famfs mount`,
`famfs fsck <daxdev>`, and `famfs logplay --daxdev`.

---

## Mode Selection API

### `famfs_select_mode()` — `famfs_misc.c` — **the authoritative policy function**

All code that needs to pick a mount implementation calls this.  Selection order (first
match wins):

1. `FAMFS_MODE` environment variable — valid values (case-sensitive):
   - `"fuse"` → `FAMFS_FUSE`
   - `"v1"` → `FAMFS_V1`
   - Any other value is ignored with a warning; detection continues.
2. Auto-detection via `famfs_get_kernel_type()` — scans `/proc/kallsyms` for famfs
   symbols.  **FUSE is preferred over V1** when both are present.

CLI flags (`--fuse` / `--nofuse`) take priority over everything and must be applied by
the caller *before* calling `famfs_select_mode()`.

Returns `FAMFS_FUSE`, `FAMFS_V1`, or `NOT_FAMFS` (kernel has no famfs support).

### `famfs_get_kernel_type()` — `famfs_misc.c`

Called by `famfs_select_mode()` for auto-detection.  Scans `/proc/kallsyms`:

1. `famfs_fuse_iomap_begin [fuse]` → `FAMFS_FUSE`
2. `famfs_create [famfsv1]` → `FAMFS_V1`
3. `famfs_create [famfs]` → `FAMFS_V1`
4. Nothing matched → `NOT_FAMFS`

Requires root (or `CAP_SYSLOG`) to read `/proc/kallsyms`.

### `famfs_module_loaded()` — `famfs_lib.c`

A V1-only check: stats `/sys/module/famfs` then `/sys/module/famfsv1`.  Used as a
**safety guard** inside `famfs_dummy_mount_v1()` and `famfs mount`'s V1 real-mount path
to confirm the module is actually loaded before attempting `mount(2)`.  Not used for mode
*selection*.

### `famfs_fuse_daemon_alive()` — `famfs_mount.c` (static)

Used during the FUSE mount superblock poll.  Stats `<shadow>/sock` and performs a live
HTTP GET to `/icache_stats` over the Unix domain socket.  Returns 1 if the daemon
responds with HTTP 200, 0 otherwise.

`famfs_fused` creates its REST socket in a separate thread, so the socket may lag the
FUSE mount by a small amount.  This function is only called after a 15-second grace
period, by which time a healthy daemon will always have created the socket.  A missing
or unresponsive socket after the grace period reliably indicates a crash.

### `famfs_daxmode_required()` — `famfs_misc.c`

Reads kernel version via `uname()`.  Returns `true` if `major > 6 || (major == 6 &&
minor >= 15)`.

Answers **"can we raw-mmap the daxdev?"** — not which mode to use, but whether raw DAX
access is blocked by the kernel.  It is the gating condition for the dummy-mount path in
mkfs, fsck, and logplay.

---

## `FAMFS_MODE` Environment Variable

`FAMFS_MODE` is the standard way for test harnesses and operators to pin a specific mount
mode without modifying CLI invocations.  It is consumed by `famfs_select_mode()` in the
library, so it affects *all* commands that call `famfs_select_mode()` — including the
internal dummy mounts in mkfs, fsck, and logplay.

| Value | Effect |
|---|---|
| `fuse` | Use FUSE mode (`famfs_fused` daemon) |
| `v1` | Use V1 standalone mode (kernel module) |
| anything else | Warning printed; falls through to auto-detection |
| unset | Auto-detection via `famfs_get_kernel_type()` |

Explicit `--fuse` / `--nofuse` CLI flags always override `FAMFS_MODE`.

---

## `enum famfs_type`

```c
enum famfs_type {
    FAMFS_MODE_UNSET = -1,  /* sentinel: no explicit mode chosen yet */
    NOT_FAMFS        =  0,  /* kernel has no famfs support */
    FAMFS_V1,               /* standalone famfs kernel module */
    FAMFS_FUSE,             /* FUSE-based famfs (famfs_fused daemon) */
};
```

`FAMFS_MODE_UNSET` is used in `do_famfs_cli_mount()` as the initial value for `fuse_mode`
so that the "no flag given" state is clearly distinguishable from `NOT_FAMFS` ("kernel
has no support").

---

## How Each Command Selects a Mode

### `famfs mount`

1. `fuse_mode = FAMFS_MODE_UNSET`
2. `--fuse` → `fuse_mode = FAMFS_FUSE`; `--nofuse` → `fuse_mode = FAMFS_V1`
3. If still `FAMFS_MODE_UNSET`: `fuse_mode = famfs_select_mode(verbose)`
   (checks `FAMFS_MODE` env var, then `/proc/kallsyms`, FUSE preferred)
4. If `NOT_FAMFS`: fail with error.
5. Dispatch to FUSE or V1 path.

### `mkfs.famfs`

1. Evaluate `no_raw_dax = nodax_in || famfs_daxmode_required()`.
2. If true: call `famfs_mkfs_via_dummy_mount()`:
   - `famfs_check_or_set_daxmode()` — enforce famfs-dax device binding.
   - `famfs_select_mode(0) == FAMFS_V1` → `famfs_dummy_mount_v1()`;
     else → `famfs_dummy_mount()`.
3. If false (kernel < 6.15, no `--nodax`): `famfs_mkfs_rawdev()` (direct mmap).

### `famfs fsck <daxdev>`

1. If `nodax || famfs_daxmode_required()`:
   - `famfs_check_or_set_daxmode()`
   - `famfs_select_mode(0) == FAMFS_V1` → `famfs_dummy_mount_v1()`;
     else → `famfs_dummy_mount()`
   - Run fsck on dummy mount, then umount.
2. Otherwise: raw mmap path.

When given a file/directory path (already-mounted), `famfs_fsck_mounted()` is called
directly — no mode detection needed, both FUSE and V1 expose identical meta files at
`.meta/.superblock` and `.meta/.log`.

### `famfs logplay --daxdev`

1. If `!famfs_daxmode_required()`: raw mmap → `__famfs_logplay()`.
2. Else:
   - `famfs_check_or_set_daxmode()`
   - `famfs_select_mode(0) == FAMFS_V1` → `famfs_dummy_mount_v1()`;
     else → `famfs_dummy_mount()`
   - `__famfs_logplay(..., FAMFS_MASTER, ...)`
   - umount.

---

## Dummy Mount Summary Table

| Command | Dummy mount needed when | Mode selection |
|---|---|---|
| `mkfs.famfs` | `nodax` or `daxmode_required()` | `famfs_select_mode()` — FUSE preferred |
| `famfs mount --dummy` | explicit `--dummy` flag | `famfs_select_mode()` via flag/env/auto |
| `famfs fsck <daxdev>` | `nodax` or `daxmode_required()` | `famfs_select_mode()` — FUSE preferred |
| `famfs logplay --daxdev` | `daxmode_required()` | `famfs_select_mode()` — FUSE preferred |

---

## Module Loading Policy

**The CLI never calls `modprobe`.** When V1 is needed and the module is not loaded, the
code prints a hint (`"try 'sudo modprobe famfs'"`) and returns an error.  Module loading
is entirely the operator's (or init system's) responsibility.

Practical consequence: on a system where both FUSE and V1 are available but the V1
module has not yet been loaded, `famfs_select_mode()` returns `FAMFS_FUSE` (since V1
symbols won't be in `/proc/kallsyms` for an unloaded module).  Setting `FAMFS_MODE=v1`
when the module is not loaded will cause `famfs_dummy_mount_v1()` to fail with a clear
error and a `modprobe` hint.

---

## Complete Call Graph

```
mkfs.famfs main()
  -> famfs_mkfs(daxdev, log_len, kill, nodax, force, set_daxmode, verbose)
       -> if (nodax || daxmode_required())
            -> famfs_mkfs_via_dummy_mount()
                 -> famfs_check_or_set_daxmode()        [sysfs / libdaxctl]
                 -> famfs_select_mode(0)                [env var → kallsyms, FUSE preferred]
                    == FAMFS_V1:
                      -> famfs_dummy_mount_v1()
                           -> famfs_module_loaded()     [safety check]
                           -> mount(2) "famfs"
                           -> __famfs_mkmeta_superblock()
                           -> mmap superblock via file
                           -> __famfs_mkmeta_log()
                    else (FAMFS_FUSE):
                      -> famfs_dummy_mount()
                           -> famfs_mount_fuse(dummy=1)
                                -> famfs_start_fuse_daemon()  [fork + execv famfs_fused]
                                -> check_file_exists() 15s    [poll for .meta/.superblock]
                                   timeout? -> famfs_fuse_daemon_alive() [stat + REST /sock]
                                     dead?  -> abort immediately
                                     alive? -> check_file_exists() 30s more
                                -> mmap superblock
                                -> __famfs_mkmeta_log()
                                -> check_file_exists() 30s    [poll for .meta/.log]
                 -> mmap sb+log via dummy mount meta files
                 -> __famfs_mkfs()                      [write sb+log to media]
                 -> famfs_umount()
       else
            -> famfs_mkfs_rawdev()                      [direct mmap, no module needed]

famfs mount do_famfs_cli_mount()
  -> fuse_mode = FAMFS_MODE_UNSET
  -> --fuse   -> fuse_mode = FAMFS_FUSE
     --nofuse -> fuse_mode = FAMFS_V1
     (none)   -> famfs_select_mode()   [FAMFS_MODE env → /proc/kallsyms, FUSE preferred]
  -> if fuse_mode == NOT_FAMFS: fail
  -> if FAMFS_FUSE:
       -> famfs_check_or_set_daxmode()
       -> if --dummy: famfs_dummy_mount()               [-> famfs_mount_fuse(dummy=1)]
          else:       famfs_mount_fuse()                [fork + exec famfs_fused, logplay]
  -> if FAMFS_V1:
       -> famfs_daxmode_required()
       -> if --dummy and daxmode_required:
            -> famfs_dummy_mount_v1()
       -> if --dummy and !daxmode_required:
            -> error (V1 dummy mount requires kernel >= 6.15 / famfs-dax mode)
       -> else (real mount):
            -> famfs_module_loaded()                    [safety check]
            -> if daxmode_required: famfs_check_or_set_daxmode()
               else:                famfs_get_role_by_dev()  [raw daxdev mmap]
            -> mount(2) "famfs"
            -> famfs_mkmeta_standalone()
            -> famfs_logplay()

famfs fsck do_famfs_cli_fsck()
  -> famfs_fsck(path, ...)
       -> stat(path)
       -> if file/dir:  famfs_fsck_mounted()            [no mode detect; uses meta files]
       -> if char/blk:
            -> famfs_get_mpt_by_dev()                   [/proc/mounts check]
            -> if (nodax || daxmode_required()):
                 -> famfs_check_or_set_daxmode()
                 -> famfs_select_mode(0) == FAMFS_V1:   famfs_dummy_mount_v1()
                    else:                               famfs_dummy_mount()
                 -> famfs_fsck_mounted()
                 -> famfs_umount()
            else: raw mmap -> famfs_fsck_scan()

famfs logplay do_famfs_cli_logplay()
  -> if --daxdev:
       -> famfs_dax_shadow_logplay()
            -> famfs_daxmode_required()
            -> if !daxmode_required: raw mmap -> __famfs_logplay()
            else:
                 -> famfs_check_or_set_daxmode()
                 -> famfs_select_mode(0) == FAMFS_V1:   famfs_dummy_mount_v1()
                    else:                               famfs_dummy_mount()
                 -> __famfs_logplay(..., FAMFS_MASTER, ...)
                 -> famfs_umount()
  -> else (mounted fs):
       -> famfs_logplay(mpt, ...)
            -> open meta files (sb, log)
            -> famfs_path_is_mount_pt()                 [check for FUSE shadow]
            -> __famfs_logplay()
```

---

## Remaining Known Issues

### Issue 6 — `famfs_dax_shadow_logplay()` hardcodes `FAMFS_MASTER`

In the dummy-mount code path the role is hardcoded to `FAMFS_MASTER`.  The raw-dax path
reads the role from the superblock.  Since the dummy mount exposes the superblock as a
file, the role could be read from it instead.

**Recommendation:** After the dummy mount, read the role from the superblock meta file
rather than hardcoding `FAMFS_MASTER`.  This would allow client-mode shadow logplay via
a daxdev.

### Issue 7 — No `--load-module` option

The CLI never calls `modprobe`.  Setting `FAMFS_MODE=v1` when the module is not loaded
will fail with a clear error, but there is no way to ask the CLI to load the module
itself.

**Recommendation (optional):** Add a `--load-module` / `-L` flag to `famfs mount` (and
possibly `famfs fsck`) that calls `modprobe famfs` (or `famfsv1`) before attempting a V1
mount.  Keep it opt-in to avoid surprising privilege escalation.
