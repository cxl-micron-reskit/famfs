# DAX Mode Management: Analysis and Rationalization Plan

## Background

On kernel >= 7.0, a DAX device must be bound to the `fsdev_dax` driver (referred to
as "famfs mode") before the famfs kernel module can mount it.  The alternative binding
is `device_dax` ("devdax mode"), which is the default state of the device and is also
the state required for direct userspace `mmap()` of the raw device.

Switching between the two modes is performed by writing to the kernel's sysfs driver
bind/unbind interface (`/sys/bus/dax/drivers/<driver>/bind` and `.../unbind`).  The
helpers for this are `famfs_get_daxdev_mode()`, `famfs_set_daxdev_mode()`, and
`famfs_daxmode_required()` in `src/famfs_dax.c` and `src/famfs_misc.c`.

Mode changes are needed (on kernel >= 7.0) during three operations:

- **`mkfs.famfs`** / **`famfs mkfs`** — must switch to famfs mode for the dummy mount
- **`famfs mount`** — must switch to famfs mode before the kernel `mount()` call
- **`famfs fsck`** — must switch to famfs mode for a dummy mount (only when called on
  an unmounted dax device)

The same operations run on pre-7.0 kernels without any mode switch, since `device_dax`
is the only available binding and direct daxdev `mmap()` still works.

Both the FUSE and standalone (famfsv1) implementations are affected.

---

## Current Mode-Change Inventory

### Sites that set `DAXDEV_MODE_FAMFS`

| Call site | Source location | Triggered when |
|-----------|----------------|----------------|
| `famfs_mount_fuse()` | `src/famfs_mount.c:787` | `daxmode_required`; applies to both real and dummy fuse mounts |
| `do_famfs_cli_mount()` | `src/famfs_cli.c:508` | `daxmode_required`; standalone v1 real mount only |
| `famfs_dummy_mount_v1()` | `src/famfs_mount.c:1053` | `daxmode_required`; standalone dummy mount |
| `famfs_dax_shadow_logplay()` | `src/famfs_lib.c:2092` | `daxmode_required`; fuse shadow logplay on unmounted daxdev |

### Sites that restore the original mode

| Call site | Source location | Comments |
|-----------|----------------|----------|
| `famfs_fsck()` | `src/famfs_lib.c:3176` | Restores `initial_daxmode` after a by-device dummy-mount fsck — the only working restore in the codebase |

---

## Bug Map

### A — No restore after failed standalone `famfs mount`

`do_famfs_cli_mount()` (the standalone path) sets famfs mode at line 508, then calls
`mount()`, `famfs_mkmeta_standalone()`, and `famfs_logplay()`.  If any of those fail,
the function jumps to `err_out` with no code to restore the original driver binding.
There is no `initial_daxmode` variable in this function.  A failed standalone mount
leaves the device permanently in famfs mode.

### B — `famfs_dummy_mount_v1()` never restores mode

`famfs_dummy_mount_v1()` unconditionally switches the device to famfs mode on entry
(when `famfs_daxmode_required()`).  It has no corresponding restore on any exit path —
not on internal error (before `mount()`) and not on success (after returning the tmpdir
to the caller).  The function implicitly delegates mode restoration to its callers, but
provides no mechanism (no out-param, no return value, no callback) for callers to know
whether a switch actually occurred.

### C — `famfs_mkfs()` has a comment but no restore code

At the end of `famfs_mkfs()` there is a comment:

```c
/* If we changed the daxmode, and we did NOT mkfs successfully,
 * put the daxdev back the way we found it
 */
return rc;
```

There is no code to implement this.  `famfs_mkfs_via_dummy_mount()` neither saves
`initial_daxmode` nor restores it.  A failed mkfs (e.g. "already has superblock")
leaves the device stuck in famfs mode.

### D — `famfs_dax_shadow_logplay()` (fuse path) never restores mode

On the `daxmode_required` path, `famfs_dax_shadow_logplay()` sets famfs mode at line
2092, performs a fuse dummy mount, plays the log, and returns.  The `out_umount` label
umounts and frees the tmpdir but never restores the original driver binding.

### E — Mode responsibility is split between `famfs_fsck()` and `famfs_dummy_mount_v1()`

`famfs_fsck()` saves `initial_daxmode` before calling `famfs_dummy_mount_v1()` and
restores it at `out_umount`.  But `famfs_dummy_mount_v1()` also sets the mode
internally.  The save and the set happen in two separate functions that are unaware of
each other's actions.  This works today but is fragile: if the internal set in
`famfs_dummy_mount_v1()` were ever made conditional on something other than
`daxmode_required()`, the restore in `famfs_fsck()` would become wrong.

### F — `famfs_mount_fuse()` does not restore mode on error

`famfs_mount_fuse()` sets famfs mode early in its execution, before shadow directory
creation, fuse daemon launch, and meta-file setup.  There are several error paths after
that point that return without restoring the original binding.  Since
`famfs_dummy_mount()` (the fuse dummy-mount wrapper) calls `famfs_mount_fuse()`, fuse
dummy mounts share this problem.

### G — `famfs_bounce_daxdev()` is declared but not implemented

`src/famfs_lib.h` declares `int famfs_bounce_daxdev(const char *devname, int verbose)`
but there is no definition anywhere in the source tree.  This is a dead declaration.

---

## Policy: What Each Operation Should Do

| Operation | Set FAMFS before | Restore on error | Restore on success |
|-----------|:---------------:|:----------------:|:-----------------:|
| `famfs mount` — real mount (v1 or fuse) | Yes | **Yes** (mount failed — no fs exists) | **No** (FAMFS mode is the correct persistent state for a mounted fs) |
| `famfs mount --dummy` | Yes | **Yes** | **Yes** (temporary operation; no fs is being left mounted) |
| `mkfs.famfs` — success | Yes | N/A | **No** (user will mount next; FAMFS mode is needed) |
| `mkfs.famfs` — failure (e.g. already has SB) | Yes | **Yes** (device should be returned to caller's original state) | N/A |
| `famfs fsck` — unmounted daxdev | Yes | **Yes** | **Yes** (read-only diagnostic; should leave no side effects) |
| `famfs logplay` — shadow/unmounted daxdev | Yes | **Yes** | **Yes** (temporary operation) |

---

## Root Cause

The fundamental problem is a **mismatch of responsibilities**: mode changes are
performed inside low-level helper functions (`famfs_dummy_mount_v1()`,
`famfs_mount_fuse()`), but mode restoration is expected from callers — without any
mechanism for callers to know whether a change occurred.  The one caller that gets it
right (`famfs_fsck()`) does so by also calling `famfs_get_daxdev_mode()` itself before
invoking the helper that changes it.  This creates redundant, fragile dual bookkeeping.
Every other caller gets it wrong.

---

## Rationalization Plan

### Core principle

**Mode management belongs at the operation level, not inside the mount helpers.**
`famfs_dummy_mount_v1()` and `famfs_mount_fuse()` should have the contract:
*"the caller ensures the daxdev is already in the required mode before calling this
function."*  This makes each function's scope clear, eliminates implicit responsibility,
and makes save/restore straightforward.

### Step 1 — Remove mode change from `famfs_dummy_mount_v1()`

Remove the `if (famfs_daxmode_required()) { famfs_set_daxdev_mode(...) }` block from
`famfs_dummy_mount_v1()`.  The function's precondition becomes: caller has already
placed the device in famfs mode.  (The `famfs_get_mpt_by_dev()` "already mounted"
check can stay; it is a correctness check, not mode management.)

### Step 2 — Remove mode change from `famfs_mount_fuse()`

Remove the equivalent block from `famfs_mount_fuse()`.  All callers
(`famfs_dummy_mount()`, `do_famfs_cli_mount()` for the fuse path) become responsible
for mode setup before calling.

### Step 3 — Introduce a save-and-set helper

Add a small helper in `src/famfs_dax.c` to avoid boilerplate in each caller:

```c
/**
 * famfs_ensure_daxmode() - Conditionally switch daxdev to target mode,
 *                          recording whether a change was made.
 *
 * Returns 0 on success (including "already in target mode"), negative errno
 * on failure.  *initial_out and *changed_out are set on success.
 */
int famfs_ensure_daxmode(const char            *daxdev,
                          enum famfs_daxdev_mode  target,
                          enum famfs_daxdev_mode *initial_out,
                          bool                  *changed_out,
                          int                    verbose);
```

Implementation: get current mode, write to `*initial_out`, compare to target, call
`famfs_set_daxdev_mode()` only if they differ, set `*changed_out`.

### Step 4 — Fix each operation

**`do_famfs_cli_mount()` (standalone, kernel >= 7.0):**

```c
enum famfs_daxdev_mode initial_daxmode;
bool mode_changed = false;

if (daxmode_required) {
    rc = famfs_ensure_daxmode(realdaxdev, DAXDEV_MODE_FAMFS,
                               &initial_daxmode, &mode_changed, verbose);
    if (rc) goto err_out;
}

rc = mount(realdaxdev, realmpt, "famfs", mflags, "");
if (rc) goto err_out_restore;

rc = famfs_mkmeta_standalone(realdaxdev, verbose);
if (rc) { famfs_umount(realmpt); goto err_out_restore; }

// success — keep FAMFS mode, fall through to logplay ...

err_out_restore:
    if (mode_changed)
        famfs_set_daxdev_mode(realdaxdev, initial_daxmode, 0);
```

**`do_famfs_cli_mount()` (fuse, kernel >= 7.0):**

Same pattern: call `famfs_ensure_daxmode()` before `famfs_mount_fuse()`, restore on
failure.

**`famfs_mkfs_via_dummy_mount()`:**

```c
enum famfs_daxdev_mode initial_daxmode;
bool mode_changed = false;

if (famfs_daxmode_required()) {
    rc = famfs_ensure_daxmode(daxdev, DAXDEV_MODE_FAMFS,
                               &initial_daxmode, &mode_changed, verbose);
    if (rc) return rc;
}

// ... famfs_dummy_mount_v1(), dummy-mount work, famfs_umount() ...

// Restore only on failure; on success the device stays in FAMFS mode
// because the caller is expected to mount immediately after mkfs.
if (mode_changed && rc != 0)
    famfs_set_daxdev_mode(daxdev, initial_daxmode, 0);
```

The placeholder comment in `famfs_mkfs()` can be removed once this is in place.

**`famfs_fsck()` (by-device, daxmode_required path):**

Replace the existing `famfs_get_daxdev_mode()` + manual check with
`famfs_ensure_daxmode()`.  The existing restore at line 3176 stays; just key it off
`mode_changed` rather than `initial_daxmode != DAXDEV_MODE_FAMFS`.  After Step 1,
`famfs_dummy_mount_v1()` no longer sets the mode internally, so there is no longer any
dual bookkeeping.

**`famfs_dax_shadow_logplay()` (fuse, daxmode_required path):**

Add `famfs_ensure_daxmode()` before the dummy mount, and unconditional restore (both
success and failure) after `famfs_umount()`.

### Step 5 — Remove the `famfs_bounce_daxdev()` stub

Delete the declaration from `src/famfs_lib.h`.

---

## Summary Table: Before vs. After

| Bug | Current behaviour | After fix |
|-----|------------------|-----------|
| Failed standalone `famfs mount` | Device left in famfs mode | Restores to original mode |
| `famfs_dummy_mount_v1()` mode leak | Sets mode, never restores | Mode-agnostic; callers own save/restore |
| `famfs mkfs` failure | Device left in famfs mode (TODO comment only) | Restores to original mode |
| `famfs_dax_shadow_logplay()` mode leak | Sets mode, never restores | Restores on both success and failure |
| Fsck dual-bookkeeping | Save in fsck + set in dummy_mount_v1 | Single save/set/restore in fsck only |
| `famfs_mount_fuse()` error paths | May leave device in famfs mode | Mode-agnostic; caller restores on failure |
| `famfs_bounce_daxdev()` dead declaration | Dangling header entry | Removed |
