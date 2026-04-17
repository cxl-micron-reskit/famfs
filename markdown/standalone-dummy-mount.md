# famfsv1 Dummy Mount Support (kernel >= 7.0)

## Background

### The raw daxdev mmap problem

Prior to kernel 7.0, famfs userspace accessed the superblock and log by opening and
`mmap()`-ing the dax device directly. This worked because the dax device was bound to
the `device_dax` driver, which permits userspace mmap.

In kernel 7.0, famfsv1 requires the dax device to be bound to the `fsdev_dax` driver
(referred to as "famfs mode"). The `fsdev_dax` driver does **not** permit userspace
`mmap()`, so the direct-mmap path is broken on 7.0.

### How fuse solved it

The famfs-fuse implementation already faced this problem. Its solution is the
**dummy mount**: mount the file system to a temporary directory (`/tmp/famfs_dummy_XXXXXX`),
create kernel-backed `.meta/.superblock` and `.meta/.log` files via the fuse shadow
mechanism, read those files to access the superblock and log, then unmount the temporary
mount point when done.

The fuse dummy mount is implemented in `famfs_mount_fuse()` (called with `dummy=1`)
and wrapped by `famfs_dummy_mount()`.  It is used by `famfs_logplay`, `famfs_fsck`,
and `famfs_mkfs` when the file system is not already mounted and direct daxdev mmap is
unavailable.

### The famfsv1 problem

When running famfsv1 on kernel 7.0, the same constraint applies: the daxdev is in
famfs mode, so userspace cannot mmap it.  The existing dummy mount infrastructure is
fuse-only, so famfsv1 needed its own equivalent.

---

## The famfsv1 Dummy Mount

### How famfsv1 meta files work

After a famfsv1 `mount()` syscall, the kernel module creates an initially empty famfs
file system at the mount point. Userspace then calls two ioctls (via
`__famfs_mkmeta_superblock()` and `__famfs_mkmeta_log()`) to create
`.meta/.superblock` and `.meta/.log` files. These ioctls configure each file as a
direct DAX mapping to a specific offset and length on the dax device — the kernel
module handles the physical access. Once created, these files can be `mmap()`-ed by
userspace just like any other file, giving safe access to the on-media superblock and
log without any direct daxdev mmap.

Crucially, the meta files are immediately visible after the ioctl returns; no polling
or `check_file_exists()` wait is needed (unlike the fuse case, where the fuse daemon
must discover the shadow files asynchronously).

### `famfs_dummy_mount_v1()` (`src/famfs_mount.c`)

```
famfs_dummy_mount_v1(realdaxdev, log_len, mpt_out, debug, verbose)
```

1. `gen_dummy_mpt()` — creates `/tmp/famfs_dummy_XXXXXX`
2. Checks that the daxdev is not already mounted
3. If `famfs_daxmode_required()` (kernel >= 7.0): calls
   `famfs_set_daxdev_mode(DAXDEV_MODE_FAMFS)` to bind the daxdev to `fsdev_dax`
4. Verifies the famfs kernel module is loaded
5. `mount(realdaxdev, tmpdir, "famfs", ...)` — real kernel mount
6. `__famfs_mkmeta_superblock()` — creates and ioctl-maps `.meta/.superblock`
7. `mmap()`s the superblock via the meta file path
8. `__famfs_get_role_and_logstats()` — reads role and log size from the superblock

**log_len semantics** (mirrors `famfs_mount_fuse()` with `dummy=1`):

| `log_len` | Meaning | Use case |
|-----------|---------|----------|
| `0` | Derive log size from superblock; validate that a valid superblock exists | fsck |
| `> 0` | Skip superblock validation; use this size; assume `FAMFS_MASTER` role | mkfs |

9. If validation passes (or is skipped): `__famfs_mkmeta_log()` — creates and
   ioctl-maps `.meta/.log`
10. Returns the tmpdir path in `*mpt_out`; caller is responsible for umount and free

On any error after `mount()`, the function unmounts and frees the tmpdir before
returning.

**Declared in** `src/famfs_lib.h`:
```c
int famfs_dummy_mount_v1(const char *realdaxdev, u64 log_len,
                          char **mpt_out, int debug, int verbose);
```

---

## Changes to the famfsv1 mount path (`src/famfs_cli.c`)

`do_famfs_cli_mount()` previously handled the famfsv1 path as follows (all kernels):

1. `famfs_get_role_by_dev()` — validated the superblock by directly mmapping the daxdev
2. `mount()` syscall
3. `famfs_mkmeta_standalone()` — created meta files; validated superblock again via file
4. `famfs_logplay()`

On kernel 7.0, step 1 fails because the daxdev is in famfs mode.

The updated logic branches on `famfs_daxmode_required()`:

**Kernel >= 7.0 (`daxmode_required == true`):**
- Skip `famfs_get_role_by_dev()` (cannot mmap daxdev)
- Call `famfs_set_daxdev_mode(DAXDEV_MODE_FAMFS)` before `mount()`
- Superblock validation occurs post-mount inside `famfs_mkmeta_standalone()`,
  which reads the superblock via the ioctl-backed meta file and unmounts on failure

**Kernel < 7.0 (`daxmode_required == false`):**
- Retain the original `famfs_get_role_by_dev()` pre-validation (direct daxdev mmap
  still works; no mode switch needed)

### `--dummy` flag with famfsv1

The `--dummy` CLI flag, previously rejected for famfsv1 with "dummy mode is
fuse-only", is now handled as follows:

| Kernel | Behavior |
|--------|----------|
| >= 7.0 | Calls `famfs_dummy_mount_v1()`; prints the tmpdir path |
| < 7.0  | Rejects with "famfsv1 dummy mount requires kernel >= 7.0" |

A famfsv1 dummy mount on pre-7.0 is unnecessary: the daxdev can be mmapped directly,
so there is no need to mount just to read the superblock.

---

## Changes to library callers (`src/famfs_lib.c`)

### Dispatch criterion: `famfs_module_loaded()`

The dispatch between `famfs_dummy_mount_v1()` and `famfs_dummy_mount()` uses
`famfs_module_loaded()` rather than `famfs_get_kernel_type()`.
`famfs_get_kernel_type()` prefers fuse (it checks for the fuse symbol first), so it
would return `FAMFS_FUSE` even on a system where famfsv1 is loaded and fuse is not
functioning correctly. `famfs_module_loaded()` checks `/sys/module/famfs` and
`/sys/module/famfsv1` directly — the presence of either means the standalone kmod is
loaded and is the right dummy mount implementation to use.

### `famfs_fsck()` (by-device path)

When `fsck` is given a dax device path and the file system is not mounted, it
previously always called `famfs_dummy_mount()` (fuse). It now dispatches based on
whether the standalone kmod is loaded:

```c
if (famfs_module_loaded(0))
    rc = famfs_dummy_mount_v1(path, 0, &dummy_mpt, 0, verbose);
else
    rc = famfs_dummy_mount(path, 0, &dummy_mpt, 0, verbose);
```

### `famfs_mkfs_via_dummy_mount()`

Same dispatch pattern:

```c
if (famfs_module_loaded(0))
    rc = famfs_dummy_mount_v1(daxdev, log_len, &mpt_out, 0, 0);
else
    rc = famfs_dummy_mount(daxdev, log_len, &mpt_out, 0, 0);
```

The `log_len > 0` path in `famfs_dummy_mount_v1()` handles the mkfs case correctly:
it skips superblock validation (there may be no valid superblock yet) and creates the
log meta file at the specified size with `FAMFS_MASTER` permissions, so mkfs can write
the superblock and log through the mmap-able meta files.

### `famfs_dax_shadow_logplay()`

Shadow logplay (playing the log into a fuse shadow directory from a raw daxdev) is a
fuse-specific operation with no famfsv1 equivalent. When `daxmode_required` is true
and the standalone kmod is loaded (`famfs_module_loaded()`), the function returns
`-ENOTSUP` immediately with a clear error message rather than attempting a fuse dummy
mount that would fail anyway.

Logplay for a mounted famfsv1 file system is unaffected: it proceeds via the normal
`.meta/.log` file path without touching the dummy mount machinery.

---

## Invariants and constraints

- `famfs_dummy_mount_v1()` requires the famfsv1 kernel module (`famfsv1.ko`) to be
  loaded. It does not attempt to load the module.
- If `famfs_daxmode_required()` returns false (kernel < 7.0), the daxdev mode switch
  is skipped; the module mounts the daxdev in its existing `device_dax` mode.
- The returned `mpt_out` tmpdir is owned by the caller: the caller must call
  `famfs_umount(mpt_out)` and `free(mpt_out)` when done. This matches the contract
  of `famfs_dummy_mount()`.
- Concurrent access is guarded: `famfs_dummy_mount_v1()` rejects the call if the
  daxdev is already mounted.
