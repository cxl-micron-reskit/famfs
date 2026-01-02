# Running ndctl Tests

This document describes how to run the ndctl/daxctl test suite, including
the famfs mode tests.

## Prerequisites

### For Non-Destructive Tests (Simulated Devices)

Non-destructive tests use kernel test modules that simulate NVDIMM and CXL
hardware. You need:

1. A kernel built with test module support (see `~/w/kconfig/ndctl/README.md`)
2. The `nfit_test` and/or `cxl_test` modules installed
3. Root privileges to load modules and run tests

```bash
# Verify test modules are available
modinfo nfit_test
modinfo cxl_test
```

### For Destructive Tests (Real Hardware)

Destructive tests operate on real DAX devices. You need:

1. A system with actual NVDIMM, CXL, or other DAX-capable hardware
2. Available DAX devices (`daxctl list` shows devices)
3. Root privileges
4. **Warning**: These tests modify device state

```bash
# Check for available DAX devices
daxctl list
```

### For famfs Tests

The famfs tests additionally require:

- The `fsdev_dax` kernel module (for famfs mode support)
- The `kmem` module (for system-ram mode transition tests)

```bash
# Verify famfs support
modinfo fsdev_dax
modinfo kmem
```

## Building ndctl with Tests

```bash
cd ~/w/famfs/ndctl

# Configure with destructive tests enabled
meson setup build -Ddestructive=enabled

# Or reconfigure existing build
meson configure build -Ddestructive=enabled

# Build
ninja -C build
```

## Running Tests

### Run All Tests

```bash
# All tests (requires root, real hardware or test modules)
sudo meson test -C build
```

### Run Tests by Suite

Tests are organized into suites: `ndctl`, `dax`, and `cxl`.

```bash
# Run only DAX tests (includes famfs)
sudo meson test -C build --suite=dax

# Run only CXL tests
sudo meson test -C build --suite=cxl

# Run only ndctl tests
sudo meson test -C build --suite=ndctl
```

### Run the famfs Test

```bash
# Run just the famfs test
sudo meson test -C build daxctl-famfs.sh

# With verbose output
sudo meson test -C build daxctl-famfs.sh -v
```

### Run Tests Manually

You can run test scripts directly for debugging:

```bash
cd ~/w/famfs/ndctl

# Set up environment
export NDCTL=build/ndctl/ndctl
export DAXCTL=build/daxctl/daxctl
export CXL=build/cxl/cxl
export TEST_PATH=build/test
export DATA_PATH=test
export LD_LIBRARY_PATH=build/daxctl/lib:build/cxl/lib:build/ndctl/lib

# Run famfs test directly
sudo -E test/daxctl-famfs.sh
```

## Test Output and Logs

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Test passed |
| 1 | Test failed |
| 77 | Test skipped (prerequisites not met) |

### Viewing Test Logs

```bash
# Full test log location
cat build/meson-logs/testlog.txt

# Last test run summary
tail -50 build/meson-logs/testlog.txt
```

## famfs Test Coverage

The `daxctl-famfs.sh` test validates:

1. **Mode transitions**
   - devdax → famfs
   - famfs → famfs (re-enable)
   - famfs → devdax
   - devdax → devdax (re-enable)

2. **JSON output**
   - Correct mode field for devdax
   - Correct mode field for famfs

3. **Error handling**
   - Invalid mode rejection

4. **System-RAM interactions** (if kmem available)
   - devdax → system-ram
   - system-ram → famfs (correctly rejected)
   - system-ram → devdax → famfs (proper path)

## Troubleshooting

### Test Skipped (Exit 77)

Check which prerequisite is missing:

```bash
# Check for fsdev_dax module
modinfo fsdev_dax || echo "fsdev_dax not available"

# Check for DAX devices
daxctl list || echo "No DAX devices found"

# Check for test modules (non-destructive)
modinfo cxl_test || echo "cxl_test not available"
```

### "Device or resource busy"

The test needs root privileges to reconfigure devices:

```bash
# Run with sudo
sudo meson test -C build daxctl-famfs.sh
```

### Test Fails on Mode Transition

Check device state:

```bash
# View current device mode
daxctl list

# Check what's using the device
lsof /dev/dax0.0
fuser -v /dev/dax0.0

# Check driver binding
ls -la /sys/bus/dax/devices/dax0.0/driver
```

### Restoring Device State

If a test fails mid-way, restore the device manually:

```bash
# Force reconfigure to devdax mode
sudo daxctl reconfigure-device -f -m devdax dax0.0
```

## Running with Test Modules (CI Environment)

For automated testing without real hardware:

```bash
# Load test modules
sudo modprobe cxl_test

# Verify simulated devices appear
daxctl list

# Run tests
sudo meson test -C build --suite=dax

# Cleanup
sudo modprobe -r cxl_test
```

## Test Development

When modifying tests, use verbose mode to see output:

```bash
# Verbose single test
sudo meson test -C build daxctl-famfs.sh -v

# Or run directly with bash tracing (already enabled via -x)
sudo -E bash -x test/daxctl-famfs.sh
```
