# famfs Mode Selection & Module Loading: Design Study

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

## Kernel Mode Detection

There are two independent detection mechanisms.  They answer different questions and are
never unified into a single call site.

### `famfs_get_kernel_type()` — `famfs_misc.c`

Scans `/proc/kallsyms` for specific symbols with their module names.

1. Searches for `famfs_fuse_iomap_begin [fuse]` → returns `FAMFS_FUSE`.
2. Falls back to `famfs_create [famfsv1]` → returns `FAMFS_V1`.
3. Falls back to `famfs_create [famfs]` → returns `FAMFS_V1`.
4. Nothing matched → returns `NOT_FAMFS`.

**FUSE is unconditionally preferred.** This is the only function that treats "which mode?"
as a single unified question.  It is used only by `famfs mount` auto-detection.

Note: `/proc/kallsyms` requires root (or `CAP_SYSLOG`).  If unreadable the function
returns `NOT_FAMFS`, which the caller will treat as "kernel not famfs-enabled."  This is
correct for commands that require root anyway, but the silent failure could be confusing
in principle.

### `famfs_module_loaded()` — `famfs_lib.c`

A pure V1 check.  Stats `/sys/module/famfs` then `/sys/module/famfsv1`.  Returns 1 or 0.
Knows nothing about FUSE.

Used by mkfs, fsck, and logplay to choose between `famfs_dummy_mount_v1()` and
`famfs_dummy_mount()`.

### `famfs_daxmode_required()` — `famfs_misc.c`

Reads kernel version via `uname()`.  Returns `true` if `major > 6 || (major == 6 &&
minor >= 15)`.

This answers **"can we raw-mmap the daxdev?"** — not which mode to use, but whether raw
DAX access is blocked by the kernel.  It is the gating condition for the dummy-mount path
in mkfs, fsck, and logplay.

---

## How Each Command Selects a Mode

### `famfs mount`

1. Check `--fuse` / `--nofuse` flags → set `fuse_mode` explicitly.
2. If neither flag given: call `famfs_get_kernel_type()` (FUSE preferred).
3. If result is `NOT_FAMFS`: fail with error.
4. Dispatch to FUSE or V1 path.

`getenv("FAMFS_MODE")` is called and the value printed, but it **has no effect on mode
selection** (see Issue 2 below).

### `mkfs.famfs`

1. Evaluate `no_raw_dax = nodax_in || famfs_daxmode_required()`.
2. If true: call `famfs_mkfs_via_dummy_mount()`:
   - Call `famfs_check_or_set_daxmode()` to enforce famfs-dax device binding.
   - Call `famfs_module_loaded()` → if true, `famfs_dummy_mount_v1()`; else
     `famfs_dummy_mount()`.
3. If false (kernel < 6.15, no `--nodax`): call `famfs_mkfs_rawdev()` (direct mmap, no
   module needed).

`famfs_get_kernel_type()` is never called.

### `famfs fsck <daxdev>`

Identical to mkfs for the raw-device path:

1. If `nodax || famfs_daxmode_required()`:
   - `famfs_check_or_set_daxmode()`
   - `famfs_module_loaded()` → `famfs_dummy_mount_v1()` or `famfs_dummy_mount()`
   - Run fsck on the dummy mount, then umount.
2. Otherwise: raw mmap path.

When given a file or directory path (already-mounted filesystem), `famfs_fsck_mounted()`
is called directly with no mode detection at all — both FUSE and V1 expose identical meta
files at `.meta/.superblock` and `.meta/.log`.

### `famfs logplay --daxdev`

Same pattern as mkfs and fsck:

1. If `!famfs_daxmode_required()`: raw mmap → `__famfs_logplay()`.
2. Else:
   - `famfs_check_or_set_daxmode()`
   - `famfs_module_loaded()` → `famfs_dummy_mount_v1()` or `famfs_dummy_mount()`
   - `__famfs_logplay(..., FAMFS_MASTER, ...)` (role hardcoded — see Issue 6)
   - umount.

---

## Dummy Mount Summary Table

| Command | Dummy mount needed when | V1 vs FUSE selection |
|---|---|---|
| `mkfs.famfs` | `nodax` or `daxmode_required()` | `famfs_module_loaded()` → V1 else FUSE |
| `famfs mount --dummy` | explicit `--dummy` flag | `famfs_get_kernel_type()` → FUSE preferred |
| `famfs fsck <daxdev>` | `nodax` or `daxmode_required()` | `famfs_module_loaded()` → V1 else FUSE |
| `famfs logplay --daxdev` | `daxmode_required()` | `famfs_module_loaded()` → V1 else FUSE |

---

## Module Loading Policy

**The CLI never calls `modprobe`.** When V1 is needed and the module is not loaded, the
code prints a hint (`"try 'sudo modprobe famfs'"`) and returns an error.  Module loading
is entirely the operator's (or init system's) responsibility.

Practical consequence: on a system where both FUSE and V1 are available but the V1
module has not been loaded yet, `famfs_module_loaded()` returns 0 and mkfs/fsck/logplay
fall through to the FUSE dummy-mount path.  This is correct behavior but may surprise
someone who expects V1.

---

## Complete Call Graph

```
mkfs.famfs main()
  -> famfs_mkfs(daxdev, log_len, kill, nodax, force, set_daxmode, verbose)
       -> if (nodax || daxmode_required())
            -> famfs_mkfs_via_dummy_mount()
                 -> famfs_check_or_set_daxmode()        [sysfs / libdaxctl]
                 -> if famfs_module_loaded()             [/sys/module/famfs*]
                      -> famfs_dummy_mount_v1()
                           -> famfs_module_loaded()     [re-check, safety net]
                           -> mount(2) "famfs"
                           -> __famfs_mkmeta_superblock()
                           -> mmap superblock via file
                           -> __famfs_mkmeta_log()
                    else
                      -> famfs_dummy_mount()
                           -> famfs_mount_fuse(dummy=1)
                                -> famfs_start_fuse_daemon()  [fork + execv famfs_fused]
                                -> check_file_exists()        [poll for .meta/.superblock]
                                -> mmap superblock
                                -> __famfs_mkmeta_log()
                                -> check_file_exists()        [poll for .meta/.log]
                 -> mmap sb+log via dummy mount meta files
                 -> __famfs_mkfs()                      [write sb+log to media]
                 -> famfs_umount()
       else
            -> famfs_mkfs_rawdev()                      [direct mmap, no module needed]

famfs mount do_famfs_cli_mount()
  -> getenv("FAMFS_MODE")                               [read-only, IGNORED]
  -> --fuse   -> fuse_mode = FAMFS_FUSE
     --nofuse -> fuse_mode = FAMFS_V1
     (none)   -> famfs_get_kernel_type()                [/proc/kallsyms, FUSE preferred]
  -> if fuse_mode == NOT_FAMFS: fail
  -> if FAMFS_FUSE:
       -> famfs_check_or_set_daxmode()
       -> if --dummy: famfs_dummy_mount()               [-> famfs_mount_fuse(dummy=1)]
          else:       famfs_mount_fuse()                [fork + exec famfs_fused, logplay]
  -> if FAMFS_V1:
       -> famfs_daxmode_required()
       -> if --dummy and daxmode_required:
            -> famfs_dummy_mount_v1()
       -> if --dummy and !daxmode_required: error
       -> else (real mount):
            -> famfs_module_loaded()
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
                 -> if famfs_module_loaded(): famfs_dummy_mount_v1()
                    else:                    famfs_dummy_mount()
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
                 -> if famfs_module_loaded(): famfs_dummy_mount_v1()
                    else:                    famfs_dummy_mount()
                 -> __famfs_logplay(..., FAMFS_MASTER, ...)
                 -> famfs_umount()
  -> else (mounted fs):
       -> famfs_logplay(mpt, ...)
            -> open meta files (sb, log)
            -> famfs_path_is_mount_pt()                 [check for FUSE shadow]
            -> __famfs_logplay()
```

---

## Issues and Recommended Improvements

### Issue 1 — Inconsistent mode preference (most important)

`famfs mount` uses `famfs_get_kernel_type()` which **prefers FUSE**.  Every other
dummy-mount site uses `famfs_module_loaded()` which **prefers V1**.  On a system with
both available simultaneously, `famfs mount` mounts as FUSE while `mkfs.famfs` does its
internal dummy mount as V1.  They produce the same on-disk result, but the inconsistency
is a latent bug and a readability problem.

**Recommendation:** Consolidate into a single `famfs_select_mode()` helper that embeds
one policy and is called from all dummy-mount decision points.  The preferred policy is
probably "FUSE first" (matching `famfs_get_kernel_type()`), since FUSE is the forward
path.  Have `famfs_module_loaded()` relegated to "is the V1 module present?" checks only,
not mode selection.

### Issue 2 — `FAMFS_MODE` environment variable is read and ignored

`do_famfs_cli_mount()` calls `getenv("FAMFS_MODE")` and prints the value but does nothing
with it.  The smoke test scripts pass `FAMFS_MODE` to communicate mode preference, but the
CLI itself does not respond to it.

**Recommendation:** Either implement it — map `"fuse"` → `FAMFS_FUSE`, `"v1"` →
`FAMFS_V1`, and assign to `fuse_mode` before auto-detection — or remove the dead
`getenv` call.  Implementing it would let the sniff/smoke harness drop its own
`--fuse`/`--nofuse` plumbing and rely on the environment variable instead.

### Issue 3 — `fuse_mode == 0` aliases `NOT_FAMFS`

In `do_famfs_cli_mount()`, the "no flag given" initial state is `fuse_mode = NOT_FAMFS =
0`.  The check `if (fuse_mode == 0)` tests "was neither flag specified?" by exploiting
`NOT_FAMFS == 0`.  This is correct today but fragile.

**Recommendation:** Add a `FAMFS_MODE_UNSET = -1` sentinel (or a separate `bool
mode_specified` flag) to make the logic explicit.

### Issue 4 — Misleading error message in V1 dummy mount gate

When `fuse_mode == FAMFS_V1`, `--dummy` is requested, and `daxmode_required()` is false,
the error says `"famfsv1 dummy mount requires kernel >= 7.0"`.  But `daxmode_required()`
triggers at >= 6.15, not 7.0.  The message also doesn't explain why the dummy mount is
gated on daxmode.

**Recommendation:** Fix the version number to 6.15 and clarify: the V1 dummy mount
requires the device to be in famfs-dax mode, which is only enforced on kernel >= 6.15.

### Issue 5 — FUSE daemon launch is fire-and-forget; 1000-second poll

`famfs_mount_fuse()` forks, execs `famfs_fused`, then polls for `.meta/.superblock` to
appear with a 1000-second timeout.  If the daemon crashes silently, the parent hangs for
~16 minutes.  There is no `waitpid()`.

**Recommendation:** Call `waitpid(WNOHANG)` in the poll loop to detect a crashed child
and return immediately with a clear error.  The long timeout may be appropriate for large
devices but should at minimum emit periodic progress output.

### Issue 6 — `famfs_dax_shadow_logplay()` hardcodes `FAMFS_MASTER`

In the dummy-mount code path the role is hardcoded to `FAMFS_MASTER`.  The raw-dax path
reads the role from the superblock.  Since the dummy mount exposes the superblock as a
file, the role could be read from it instead.

**Recommendation:** After the dummy mount, read the role from the superblock meta file
(as `famfs_dummy_mount_v1()` already makes it available) rather than hardcoding
`FAMFS_MASTER`.  This aligns the dummy-mount path with the raw path and would allow
client-mode shadow logplay via a daxdev.

### Issue 7 — No `--load-module` option

The CLI never calls `modprobe`.  The smoke test scripts load the module themselves before
invoking the CLI.  If the module is absent when V1 is wanted, `famfs_module_loaded()`
returns 0 and the code silently falls through to the FUSE path.

**Recommendation (optional):** Add a `--load-module` / `-L` flag to `famfs mount` (and
possibly `famfs fsck`) that calls `modprobe famfs` (or `famfsv1`) before attempting a V1
mount.  Keep it opt-in to avoid surprising privilege escalation.

---

## The Clean Picture

For all commands that need to select a mount mode, the logic should be:

```
1. Was --fuse / --nofuse given?          → use that.
2. Is FAMFS_MODE set in the environment? → use that (currently ignored).
3. Auto-detect via famfs_get_kernel_type() → FUSE preferred over V1.
4. If NOT_FAMFS: fail with a clear error.
```

Steps 1–4 should produce a single `enum famfs_type` that is used consistently by **all**
callers, including the dummy-mount dispatch in mkfs, fsck, and logplay.  Today steps 3–4
only happen in `famfs mount`; the other commands re-derive the mode using
`famfs_module_loaded()`, which is a V1-only check that misses step 3's FUSE preference.
