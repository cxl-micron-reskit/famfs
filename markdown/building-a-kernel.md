<p align="center">
  <img src="famfs-logo.svg" alt="famfs logo">
</p>

# Building and installing a famfs-compatible Linux kernel

Clone the kernel. The kernel below has patches that enable testing with /dev/dax devices,
but are not used with /dev/pmem devices. (Running with /dev/pmem devices is currently
recommended.)

    git clone https://github.com/cxl-micron-reskit/famfs-linux.git

Put an appropriate .config file in place

    cp famfs/kconfig/famfs-config-6.14 famfs-linux/.config

Note: the ```famfs/kconfig``` subdirectory in the famfs user space tree
has appropriate kernel config files for some
kernels - e.g. the famfs-config-6.8 config file is appropriate for a kernel with the
famfs v1 patch set. We recommend choosing the closest config file at or below the
version of your kernel.

Build the kernel

    cd famfs-linux
    make oldconfig            # Enable famfs-related options
    make -j $(nproc)

At this point it is likely that you will need to install prerequisites and re-run
the kernel build.

Once you have successfully built the kernel, install it.

    sudo make modules_install headers_install install INSTALL_HDR_PATH=/usr

Reboot the system and make sure you're running the new kernel

    uname -r

Output (as of May 2025):

    6.14.0+

# Famfs Standalone vs. Fuse

Famfs was introduced in 2024 as a standalone file system - but metadata management was
always performed from user space. In the famfs session at LSFMM 2024, the consensus was that
famfs should be ported into FUSE. This was a non-trivial undertaking, but the first 
[famfs-fuse patch set](https://lore.kernel.org/linux-fsdevel/20250421013346.32530-1-john@groves.net/T/#m16f1386e90a6b40ceb60ae7feca7bbff281956bc)
was released in April 2025.

The fuse implementation of famfs was a major undertaking from the standalone version, but one
thing has stayed almost identical: file metadata is cached in the kernel such that mapping faults
can be serviced in-kernel without upcalls. 

The current situation is that the famfs user space (this repo) works with both kinds of famfs
kernels: FUSE and standalone.

# Kernel Branches

The famfs kernel repo contains several branches; here are the important ones as of May 2025.

‼️Note that none of these branches should be considered "stable". They will be rebased as the famfs kernel components get improved and updated.

| **Branch** | **Notes** |
|------------|-------------|
| ```famfs-6.14``` | The famfs fuse-mode kernel |
| ```famfs_dual``` | A dual-mode kernel, which can run famfs either via fuse or via the standalone fs/famfs file system. The standalone famfs in this branch has all fixes and improvements that have been applied since the last standalone famfs patchset (05/24). |
