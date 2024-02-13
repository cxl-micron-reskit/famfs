# Famfs utility scripts

Note the some of these scripts may need to be run from the directory above, e.g. `scripts/teardown.sh`, in order to resolve paths correctly.

| **Script** | **Description** |
|:----------|:--------------|
| `check_valgrind_output.sh` | Looks for errors in a log containing valgrind tests, and prints just the valgrind outputs containing the errors |
| `chk_efi.sh` | Check whether the running system is properly configured with efi firmware |
| `chk_include.sh` | Install famfs_ioctl.h include file if necessary |
| `chk_memdev.sh` | Check for valid dax device configuration |
| `fc_disable.sh` | Disable the fault counters in famfs |
| `fc_enable.sh` | Enable the fault counters in famfs |
| `fc_print.sh` | Print the fault counters from famfs |
| `mkfs.sh` | Example script that creates a famfs file system |
| `mount.sh` | Example script to mount a famfs file system |
| `setup.sh` | Utility script to load the famfs kernel module and mount a file system as used by the smoke tests |
| `teardown.sh` | Utility script to unmount famfs and unload the kernel module |
| `test_funcs.sh` | Utility functions that are sourced into many other scripts |
