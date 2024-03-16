<p align="center">
  <img src="markdown/famfs-logo.svg" alt="famfs logo">
</p>

# Building and installing a famfs-compatible Linux 6.7 kernel

Clone the kernel. The kernel below has patches that enable testing with /dev/dax devices,
but are not used with /dev/pmem devices. (Running with /dev/pmem devices is currently
recommended.)

    git clone https://github.com/cxl-micron-reskit/famfs-linux.git

Build the kernel

    cd famfs-linux
    cp famfs.config .config   # Put appropriate config file in place
    make oldconfig            # Accept defaults, if any
    make [-j]

At this point it is likely that you will need to install prerequisites and re-run
the kernel build.

Once you have successfully built the kernel, install it.

    sudo make modules_install headers_install install INSTALL_HDR_PATH=/usr

Reboot the system and make sure you're running the new kernel

    uname -r

Output (as of Feb 2024):

    6.8.0-rc4+

