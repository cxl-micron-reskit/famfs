<p align="center">
  <img src="famfs-logo.svg" alt="famfs logo">
</p>

# Configuring Virtual Machines for famfs

In order to run famfs, you will need at least one physical or virtual devdax memory device
(i.e. a ```/dev/dax0.0``` memory device in ```devdax``` mode).

# Prerequisites

You will need to install the following packages:

- ndctl
- daxctl

:bangbang: Earlier versions of this document said VMs must be in EFI mode; this is no longer true, as the efi_fake_mem kernel parameter was removed.

# Creating Simulated Memory Devices

## Configuring a non-shared simulated pmem device

If you just want to build famfs and run basic tests, this is the easiest thing to do.

The easiest way to configure a **non-sharable** pmem device is to edit your /etc/default/grub file and
add the following to the GRUB_CMDLINE string:

    memmap=8G!8G

:bangbang: **Important:** For the command line above, you must have at least 16G of memory, because you are reserving 8G starting at a starting offset of 8G. Linux does not error-check this; if you try to use nonexistent memory we observes that it both runs slowly and doesn't work.

**Note:** In theory ```memmap=8G$8G``` should give you a dax device rather than pmem. The usual problem is that it's hard to successfully escape the ```$``` so this often fails. Here is good solution:

    # Create a memmap_val grub macro:
    sudo grub2-editenv /boot/grub2/grubenv set memmap_val='8G\$8G'
    # Use the macro in /etc/default/grub
    ...memmap=\$memmap_val

The latter will reserve 8G of memory at offset 8G into system physical RAM as a
simulated devdax device (while the former will provide a simulated pmem device).
You must check your available memory and make sure that you have enough to
back the entire range - if not, it won't work well...

After doing that, you will need too run the following commands:

    sudo grub2-mkconfig -o /boot/grub2/grub.cfg          # if your vm is not in EFI mode
    sudo grub2-mkconfig -o /boot/efi/EFI/fedora/grub.cfg # if your vm is in EFI mode
    sudo reboot

After a reboot, you should see a pmem device.

```
$ ndctl list
[
  {
    "dev":"namespace0.0",
    "mode":"raw",
    "size":8573157376,
    "uuid":"1acedc2a-cf5d-4f20-8e34-f1909e0286a5",
    "sector_size":512,
    "blockdev":"pmem0"
  }
]
```

## Converting a pmem device to /dev/dax

Although early versions of famfs worked with either pmem or devdax devices, famfs now
requires devdax. But a pmem device can be converted to devdax mode.

## Configuring a simulated /dev/dax device

There are two reasons you might want to create a simulated dax device. One
is for a private dax device within a VM. The other is to create a dax device on a host,
to be shared with one or more VMs. Either way, the procedure is the same.

Add the following to your kernel command line:

    efi_fake_mem=8G@8G:0x40000 memhp_default_state=offline

The first argument marks an 8G range at offset 8G into physical memory with the EFI_MEMORY_SP bit,
which will cause Linux to treat it as a dax device. The second argument asks Linux not to
automatically online the dax device after boot.

**NOTE:** Your system must be configured for EFI firmware for this option to work. You can check that by running ```scripts/chk_efi.sh```.

As in the prior section, after you modify the kernel command line you need to update grub:

    sudo grub2-mkconfig -o /boot/efi/EFI/fedora/grub.cfg
    sudo reboot

**NOTE:** If you specify an offset and length that don't fall within system memory, we have
observed cases where the device is still created, but (of course) it does not work
properly. Just don't do that.

**NOTE:** There are some circumstances where udev rules installed by your Linux distro may "online" your dax memory as
system-ram after boot.

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
```
Note that the dax device above is in "system-ram" mode, which seems to be the default. To
use it with famfs you must convert it to "devdax" mode as follows:
```
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

# Creating VMs that share a virtual devdax device from the host

This recipe works as follows:
1. In the hypervisor system that will host the VMs, create a "container" for the VM's dax device. This can be any of the following:
    - A file (there are advantages to this if you aren't using real CXL memory)
    - A simulated devdax device in the hypervisor host
    - An actual CXL devdax device attached to the hypervisor host
2. Configure one or more VMs that have a virtual pmem device backed by a file or /dev/dax device in the host. More than one VM can have a virtual pmem device backed by the same file or device in the hypervisor, resulting in a shared memory device in the VMs.
3. Inside the VMs, convert the shared pmem devices to devdax mode.

## Background

Qemu supports virtio pmem devices, which can be backed by a shared (or non-shared) file or devdax device -- but it
does not support virtual devdax devices. This is usable because a pmem device can be converted
to devdax mode, at which point a fully functional devdax device (e.g. ```/dev/dax0.0```) will
appear in its place.

## Preparation

Editing libvirt xml is tedious. We recommend that you start with a working VM in efi mode,
and use git to manage changes to your libvirt xml files (which are normally located in
```/etc/libvirt/qemu```. This will allow you to commit working
versions and revert if you break it. But maybe these instructions will be so good that it will
be easy. It was a real pain to figure out... :D

## Create a libvirt VM

The easiest way to create a libvirt VM is to install it from a distro
ISO image via virt-manager. 

## Prepare a virtual devdax backing file if needed

If you will not be mapping a devdax to your VM, it is convenient to use a 
backing file for your virtual pmem/devdax devices. You can create an 8GiB
backing file this way:

    dd if=/dev/zero of=/var/lib/libvirt/images/f42-pmem-backing bs=1M count=8192

Note it is traditional for VM storage to reside in /var/lib/libvirt/images, but
it's not required.

Qemu/KVM will mmap the backing file and provide it as a pmem/devdax device to the VM.

## Preparing to share a memory device across 2 or more VMs

In this example we will share an 8GiB /dev/dax device from the hypervisor. Each VM will have
a virtual pmem device backed by the same /dev/dax device. The /dev/dax device can be either
physical or virtual.

We will use ```virsh edit``` to edit the VM

    virsh edit f42-dev2

### The xmlns nonsense

At the top of your xml, you will see a <domain> tag.
```
<domain type='kvm'>
  <name>f42-dev2</name>
  ...
```

If it does not include "xmlns", you should add it:
```
<domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <name>f42-dev2</name>
  ...
```
Failing to include this can cause libvirt to fail to validate the xml.

### System memory description

In this example, my VM has 32GiB of general purpose memory, and I will be sharing an 8GiB
/dev/dax device from the hypervisor (as a pmem device in the VM).

Your memory description needs to be as follows:
```
  ...
  </metadata>
  <maxMemory slots='2' unit='GiB'>40</maxMemory>
  <memory unit='GiB'>40</memory>
  <currentMemory unit='GiB'>40</currentMemory>
  ...
```
**Note:** maxMemory, memory and currentMemory are all required, and the size of all three must be the
sum of the general purpose memory and the shared device. In this example, general purpose memory
is 32G and the dax device is 8G.

**NOTE:** Libvirt will convert the units from GiB to KiB, but using GiB when editing is somewhat
less error-prone.

You must also create a numa node. This is a libvirt xml requirement in order to make the dax-backed
pmem device work.
```
  </features>
  <cpu mode='host-passthrough' check='none' migratable='on'>
    <numa>
      <cell id='0' cpus='0-1' memory='32' unit='GiB'/>
    </numa>
  </cpu>
```

:bangbang:**NOTE:** The size of the numa node should match the size of the general purpose memory 
excluding the size of the shared dax device.

**NOTE:** The ```cpus``` should be ``'0-n'`` for n+1 cpus. Set this correctly for your VM.

### Virtual pmem device description

The following stanza should be added as the last entry in the <devices> section.
```
    ...
    <memory model='nvdimm' access='shared'>
      <source>
        <path>/var/lib/libvirt/images/f42-pmem-backing</path>
        <alignsize unit='MiB'>2</alignsize>
        <pmem/>
      </source>
      <target>
        <size unit='GiB'>8</size>
        <node>0</node>
        <label>
          <size unit='MiB'>16</size>
        </label>
      </target>
      <address type='dimm' slot='1'/>
    </memory>
  </devices>
```
Notes:
- path should reference the backing file, or the shared dax device in the hypervisor host
- alignsize must be specified as 2MiB (or 2048 KiB)
- size must match the size of the dax device

### Test your pmem device

If you edited the above sections correctly, and updated grub, and rebooted...  a miracle will
occur and your VM will have a working pmem device.

```
$ ndctl list
[
  {
    "dev":"namespace0.0",
    "mode":"raw",
    "size":8573157376,
    "uuid":"1acedc2a-cf5d-4f20-8e34-f1909e0286a5",
    "sector_size":512,
    "blockdev":"pmem0"
  }
]
```
Note that the device will probably start out in raw mode. In order to use it you will need to convert to
devdax mode. Using fsdax mode will give you a /dev/pmem device.

```
$ sudo ndctl create-namespace --force --mode=devdax --reconfig=namespace0.0
{
  "dev":"namespace0.0",
  "mode":"devdax",
  "map":"dev",
  "size":"7.86 GiB (8.44 GB)",
  "uuid":"8c2894ed-ba4a-45c5-bc1b-8b9fe8412f7e",
  "daxregion":{
    "id":0,
    "size":"7.86 GiB (8.44 GB)",
    "align":2097152,
    "devices":[
      {
        "chardev":"dax0.0",
        "size":"7.86 GiB (8.44 GB)",
        "target_node":0,
        "align":2097152,
        "mode":"devdax"
      }
    ]
  },
  "align":2097152
}
```
At this point you should see a usable devdax device:
```
$ daxctl list
[
  {
    "chardev":"dax0.0",
    "size":8438939648,
    "target_node":0,
    "align":2097152,
    "mode":"devdax"
  }
]
```

