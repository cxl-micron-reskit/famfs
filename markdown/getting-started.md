
# Getting started with famfs

# Quick Links
- [Famfs CLI Reference](famfs-cli-reference.md)
- [Buillding a famfs-compatible kernel](building-a-6.7-kernel.md)
- [Famfs Usage - Getting Started](famfs-usage.md)

# Preparing to build famfs
Famfs is comprised of a kernel module and a set of user space tools and libraries.
The whole thing can be built from the top directory of the repo, but there are a
few prerequisites that must be met.

* You must currentlly be running a Linux 6.7 kernel. See
  [Buillding a famfs-compatible kernel](building-a-6.7-kernel.md)
* You need the appropriate set of development tools. On fedora 39, the following are
  required
    - Group: "Development Tools"
    - Packages: kernel-devel, zlib, libuuid-devel, (what else?)
* The kernel source (or kernel-devel package for your stock kernel) is required in order
  to build the famfs kernel module.
  If you are running a stock distro kernel, it is probably not Linux 6.7. So you
  probably need to build and install the kernel linked above. If you have installed that
  and are running it, the prereqs for building the famfs kernel module should be met.

## Known Prerequisites

### On a Server for VMs Running Famfs

In order to run VMs that mount famfs file systems on shared dax devices, you should be running
VMs in efi firmware mode.
```
sudo dnf install edk2-ovmf
```

### Fedora 39 Prerequisites
On fedora, you probably need to install the following, at minimum
```
sudo dnf groupinstall "Development Tools"
sudo dnf install kernel-devel
sudo dnf install cmake
sudo dnf install g++
sudo dnf install libuuid-devel
```
Pay attention to error messages when you build, new dependencies may arise later, and
different kernel installations may have different missing dependencies.

### Ubuntu 23 Prerequisites

```
sudo apt install build-essential
sudo apt install cmake
sudo apt install uuid-dev
sudo apt install zlib1g-dev
sudo apt install daxctl ndctl
```
Pay attention to error messages when you build, new dependencies may arise later, and
different kernel installations may have different missing dependencies.

# Building famfs

From the top level directory:

    make clean all

This command also works in the ```kmod``` and ```user``` directories, and just builds the kmod
or user code respectively

# Installing famfs

You must load the kernel module in order to use famfs

    sudo insmod kmod/famfs.ko

Famfs does not have packaged installation files yet, once you have built it, you can
install the user space library and cli with the following command:

    cd user; sudo make install

# Preparing to run famfs

In order to run famfs you need either a pmem device (e.g. /dev/pmem0) or a
devdax device (e.g. /dev/dax0.0). To run in a shared mode, you need more than one
system that shares a memory device.

As of early 2024, the following types of memory devices can host famfs:

* A /dev/pmem device can host a famfs file system
* A /dev/dax device can host a famfs file system - if the you are running a kernel with the
  dev_dax_iomap patches. This is needed because devdax in mainline does not yet support
  the iomap API (dax_iomap_fault/dax_iomap_rw) that is required by fsdax file systems. The
  famfs kernel patch set addresses this deficiency

:bangbang: **Important:** as of early 2024 /dev/dax devices that were converted from /dev/pmem are not compatible with famfs due to a kva horkage bug. We expect to fix this kernel bug in the near future


## Preparing to create simulated /dev/dax and /dev/pmem devices

This documentation assumes that your system is configured with EFI firmware and boot
process. This can be non-trivial with VMs, but please do it with both your physical
and virtual machines. You can check whether your system is properly configured for efi
by running the following script:

    sudo user/scripts/chk_efi.sh

You will need to edit the kernel command line and then tell the system to put that into
the config file that grub reads during boot. This does not work the same on all distros,
so you may need to become an expert.

## Configuring a simulated /dev/dax device
There are two reasons you might want to create a simulated dax device. One
is for a private dax device within a VM. The other is to create a dax deice on a host,
to be shared with one or more VMs. Either way, the procedure is the same.

Add the following to your kernel command line:

    efi_fake_mem=8G@8G:0x40000 memhp_default_state=offline

The first argument marks an 8G range at offset 8G into physical memory with the EFI_MEMORY_SP bit,
which will cause Linux to treat it as a dax device. The second argument asks Linux not to
automatically online the dax device after boot.

*NOTE:* If you specify an offset and length that don't fall within system memory, we have
observed cases where the device is still created, but (of course) it does not work
properly. Just don't do that.

*NOTE:* There are some circumstances where udev rules may "online" your dax memory as
system-ram after boot. YOu

After boot it is likely that your /dev/dax device will be in system-ram mode; if so,
you will need to convert it to "devdax" mode.

```
$ daxctl list
[
  {
    "chardev":"dax1.0",
    "size":8589934592,
    "target_node":0,
    "align":2097152,
    "mode":"system-ram"
  }
]
$ sudo daxctl reconfigure-device --mode=devdax dax1.0
[
  {
    "chardev":"dax1.0",
    "size":8589934592,
    "target_node":0,
    "align":2097152,
    "mode":"devdax"
  }
]
reconfigured 1 device
```

## Configuring a non-shared simulated pmem device

If you just want to build famfs and run basic tests, this is the easiest thing to do.

The easiest way to configure a pmem device is to edit your /etc/default/grub file and
add the following to the GRUB_CMDLINE string:

    memmap=8G!8G

:bangbang: **Important:** For the command line above, you must have at least 16G of memory, because you are reserving 8G starting at a starting offset of 8G. Linux does not error-check this; if you try to use nonexistent memory we observes that it both runs slowly and doesn't work.

This will reserve 8G of memory at offset 24G into system physical RAM as a simulated
pmem device. You must check your available memory and make sure that you have enough to
back the entire range - if not, it won't work well...

AFter doing that, you willl need too run the following command (assuming your system
is in efi mode:

    sudo grub2-mkconfig -o /boot/efi/EFI/fedora/grub.cfg
    sudo reboot

After a reboot, you should see a pmem device.

## Configuring a set of VMs to share a simulated pmem device

The best way to share a memory device is:

* Create a simulated dax device on the hypervisor host (or use a real dax device)
* Create VMs that have a simulated /dev/pmem device backed by the dax device
* Within the VMs, the pmem device can be used as pmem (in fs-dax mode) or converted to devdax
  mode, in which case it will morph into a /dev/dax device.


### Providing pmem devices to VMs via qemu/libvirt

Qemu supports emulated pmem devices, and libvirt also supports this. To create an emulated
pmem device for a VM, you can add something similar to the following to the libvirt xml
for your VM:

```
<devices> 
...
   <memory model='nvdimm' access='shared'>
      <source>
        <path>/dev/dax1.0</path>
        <pmem/>
      </source>
      <target>
        <size>1073739904</size>
        <node>0</node>
        <label>
          <size>1073739904</size>
        </label>
      </target>
      <address type='dimm' slot='1'/>
    </memory>
 ...
 </devices>
 ```
Note that the size matches the ```/dev/dax1.0``` device in the example above.

### Providing *shared* pmem devices via qemu/libvirt

Providing shared pmem devices to more than one VM can be done by simply adding the same
xml (i.e. the same backing file or device) to the xml of more than one VM.

### Using pmem devices
Famfs supports running on a pmem device in fsdax mode.

### Converting pmem devices between devdax and fsdax modes
A pmem device can be used in either fsdax or pmem modes:
```
$ ndctl list
[
  {
    "dev":"namespace0.0",
    "mode":"devdax",
    "map":"dev",
    "size":8453619712,
    "uuid":"e183439a-32e8-42e1-90aa-9a8936daca30",
    "chardev":"dax0.0",
    "align":2097152
  }
]
$ sudo ndctl create-namespace --force --mode=fsdax --reconfig=namespace0.0
{
  "dev":"namespace0.0",
  "mode":"fsdax",
  "map":"dev",
  "size":"7.87 GiB (8.45 GB)",
  "uuid":"d3fa1f7a-98ff-4c1a-ab7a-abcf8ec7e83a",
  "sector_size":512,
  "align":2097152,
  "blockdev":"pmem0"
}
$ ndctl list
[
  {
    "dev":"namespace0.0",
    "mode":"fsdax",
    "map":"dev",
    "size":8453619712,
    "uuid":"d3fa1f7a-98ff-4c1a-ab7a-abcf8ec7e83a",
    "sector_size":512,
    "align":2097152,
    "blockdev":"pmem0"
  }
]
$ sudo ndctl create-namespace --force --mode=devdax --reconfig=namespace0.0
{
  "dev":"namespace0.0",
  "mode":"devdax",
  "map":"dev",
  "size":"7.87 GiB (8.45 GB)",
  "uuid":"8dd3b99d-fcb5-4963-9ad3-073e28cf85eb",
  "daxregion":{
    "id":0,
    "size":"7.87 GiB (8.45 GB)",
    "align":2097152,
    "devices":[
      {
        "chardev":"dax0.0",
        "size":"7.87 GiB (8.45 GB)",
        "target_node":0,
        "align":2097152,
        "mode":"devdax"
      }
    ]
  },
  "align":2097152
}
$ ndctl list
[
  {
    "dev":"namespace0.0",
    "mode":"devdax",
    "map":"dev",
    "size":8453619712,
    "uuid":"8dd3b99d-fcb5-4963-9ad3-073e28cf85eb",
    "chardev":"dax0.0",
    "align":2097152
  }
]
```

We are not aware of direct support for /dev/dax devices via qemu/libvirt, but you can
convert your pmem devices to /dev/dax devices as follows:



# Running tests
Famfs already has a substantial set of tests, though we plan to expand them substantially
(and we would love your help with that!).

## Running smoke tests

The famfs smoke tests load the kernel module, create and mount a famfs file system, and
test its funcionality.
The smoke tests currently must be run by a non-privileged user with passwordless sudo
enabled. Running the tests as root does not work, because they expect certain operations to
fail due to non-root privileges.

You can enable passwordless sudo for user 'fred' in your test systems and VMs as follows:

    # Substitute your username for "fred", and run this command as root
    echo "fred ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/fred

:bangbang: **Important:** If you do not enable passwordless sudo for your account, smoke tests will not run correctly. If you run the entire smoke test suite under sudo (e.g. ```sudo make smoke```), it it will not work correctly because the smoke tests expect to be running as a non-privileged user except when the smoke tests invoke sudo.

If you have already successfully built famfs and configured a pmem device, you can run smoke tests
as follows:

    cd user
    make smoke

You can also optionally specify the device as an environment variable when running smoke tests

    DEV=/dev/dax1.0 make smoke

You can see an example of the [full output from run_smoke.sh here](smoke-example.md)

The smoke tests (by default) require the following:

* A valid /dev/pmem0 device which is at least 4GiB in size

## Running unit tests

Famfs uses the googletest framework for unit testing code components, although test coverage
is still limited.

    cd user
    make test

You can also do this:

    cd user
    sudo debug/test/famfs_unit_tests

Important note: the unit tests must be run as root or under sudo, primarily because
getting the system UUID only works as root. We may look into circumventing this...

## Valgrind Checking
You can run the smoke tests under valgrind as follows:

    make smoke_valgrind

If the smoke tests complete and valgrind finds no errors, you will see a message such as this:

    Congratulations: no errors found by Valgrind

Otherwise, you will see the valgrind output from just the tests where valgrind found problems.

## Code Coverage

To build for coverage tests, do the following in user:

    make clean coverage
    make coverage_test
    firefox coverage/famfs_unit_coverage/index.html   # or any other browser

The resulting report looks like this as of the last week of December 2023.
![Image of gcov coverage report](Screenshot-2024-01-08-at-7.13.09-AM.png)

This is the combined coverage from smoke and unit tests.