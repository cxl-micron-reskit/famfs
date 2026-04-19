# famfs Test Coverage Gaps — 2026-04-18

Identified from the four commits landed today. Work through in order; delete this file
when all items are resolved or explicitly deferred.

---

## 1. Unit tests: `famfs_select_mode()` env-var logic

**File:** `test/famfs_unit.cpp`

`famfs_select_mode()` has zero unit test coverage.  It is pure logic (string comparison +
delegation) and is fully testable without hardware.  The existing `mock_*` globals already
allow `famfs_get_kernel_type()` results to be controlled.

Cases to cover:

- `FAMFS_MODE=fuse` → returns `FAMFS_FUSE`
- `FAMFS_MODE=v1` → returns `FAMFS_V1`
- `FAMFS_MODE=garbage` → prints warning, falls through to `famfs_get_kernel_type()`
- Unset → delegates to `famfs_get_kernel_type()`

**Priority:** High — low cost, high confidence.

**Status:** DONE

---

## 2. Smoke: `--load-module` flag (`famfs mount` and `famfs fsck`)

**File:** new `smoke/test_load_module.sh` (or added to `smoke/test_daxmode.sh`)

Gate on `FAMFS_MODE=v1` (or explicit `--nofuse`) and kernel >= 7.0.

Tests:

1. Unload the module (`modprobe -r famfs && modprobe -r famfsv1`).
2. `famfs mount --nofuse --load-module <dev> <mpt>` → expect success, module reloaded.
3. `famfs fsck --nofuse --load-module <dev>` → expect success.
4. Negative: `--load-module` combined with `--fuse` (FUSE-only system) should be a
   no-op — no crash, mount succeeds via FUSE.

**Priority:** Medium — most visible gap given the code just landed.

**Status:** DONE — `smoke/test_load_module.sh`, wired into `run_smoke.sh` after `test_daxmode`.
Skips gracefully when `FAMFS_MODE != v1` or module unload not enabled (`RMMOD=0`).

---

## 3. Smoke: `FAMFS_MODE=garbage` warning + fallthrough

**File:** any existing smoke test, e.g. `smoke/test0.sh` or `smoke/prepare.sh`

One or two lines: set `FAMFS_MODE=bogusvalue`, run a simple famfs command, verify it
succeeds (warning printed, falls through to auto-detection) and produces the expected
warning on stderr.

**Priority:** Low — trivial to add, mostly confirms the warning message doesn't regress.

**Status:** TODO

---

## 4. Smoke: `client_mode` path in shadow logplay dummy-mount (Issue 6 fix)

**File:** `smoke/test4.sh` or `smoke/test_daxmode.sh`

The fix in `famfs_dax_shadow_logplay()` reads the role from the superblock via the
dummy-mount rather than hardcoding `FAMFS_MASTER`.  The `client_mode` branch
(`--client` flag) is untested in both smoke and unit tests.

Gate on kernel >= 7.0 (dummy-mount path).  Steps:

1. mkfs on daxdev as master.
2. Shadow logplay with `--client` flag → verify logplay succeeds and files are mapped
   read-only (client role).
3. Shadow logplay without `--client` flag → verify master role.

**Priority:** Medium — the role-reading code path is new and exercises the dummy-mount
superblock mmap.

**Status:** TODO

---

## 5. (Deferred) Smoke: FUSE daemon crash → abort path

The two-phase health-check in `famfs_mount_fuse()` aborts immediately when
`famfs_fuse_daemon_alive()` returns 0 after the grace period.  Exercising this requires
killing `famfs_fused` after fork but before it creates `.meta/.superblock` — feasible
but fragile.

`famfs_fuse_daemon_alive()` is `static`, so not directly unit-testable without a shim.

**Priority:** Low — defer unless the crash-abort path causes a real incident.

**Status:** DEFERRED

---

## Priority Order

| # | Item | Type | Priority | Cost |
|---|------|------|----------|------|
| 1 | `famfs_select_mode()` unit tests | Unit | High | Low |
| 2 | `--load-module` smoke tests | Smoke | Medium | Medium |
| 3 | `client_mode` shadow logplay smoke | Smoke | Medium | Medium |
| 4 | `FAMFS_MODE=garbage` warning smoke | Smoke | Low | Trivial |
| 5 | FUSE daemon crash abort path | Smoke | Low | High — DEFERRED |
