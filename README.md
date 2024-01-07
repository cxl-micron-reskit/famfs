
# This is the combined kernel & userspace repo for famfs.

This will eventually split into separate repos:
* A patched kernel repo with the famfs kernel component (the kmod subdirectory here)
* A userspace repo with the userspace component (the user subdirectory here)

## Repository Contents

* *kmod* - The famfs kernel module. A Linux 6.5 kernel is currently required.
* *user* - The user space components of famfs (which is most of it)
* *scripts* - A few utiliies

## Documentation Contents

* Overview - this document
* [Getting Started with famfs](user/README.md)

# What is Famfs?

Famfs is a scale-out shared-memory file system. If two or more hosts have shared access
to memory, a famfs file system can be created in that memory, such that data sets can be
saved to files in the shared memory.

For apps that can memory map files, memory-mapping a famfs file provides direct access to
the memory without any page cache involvement (and no faults involving data movement at all).

# Background

In the coming years the emerging CXL standard will enable shared fabric-attached
memory (FAM). Famfs is intended to provide a viable usage pattern for FAM that many apps
can use without modification.

Famfs is an fs-dax file system that allows multiple hosts to mount the same file system
from the same shared memory device. The file system is administered by a Master, but can
be concurrently mounted by Clients (which default to read-only access to files, but writable
access is permitted).

Why do we need a new fs-dax file system when others (e.g. xfs and ext4) exist? Because the existing
fs-dax file systems use write-back metadata, which is not compatible with shared memory
access.

[Famfs was introduced at the 2023 Linux Plumbers Conference](https://lpc.events/event/17/contributions/1455/). The linked page contains the abstract, plus links to the slides and a
youtube video of the talk.

## What is dax?
In Linux, special purpose memory is exposed as a dax device (e.g. /dev/dax0.0 or /dev/pmem0).
Applications can memory map dax memory by opening a dax device calling the mmap() system call
on the fille descriptor.

Dax memory can be onlined as system-ram, but that is not appropriate if the memory is
shared. The first of many reasons for this is that Linux zeroes memory that gets onlined,
which would wipe any shared contents.

In CXL V3 and beyond, dynamic capacity devices (DCDs) support shared memory. Sharable memory
has a mandatory Tag (UUID) which is assigned when the memory is allocated;
all hosts with shared access identify the memory by its Tag.
"Tagged Capacity" will be exposed under Linux as tagged dax devices
(e.g. /sys/devices/dax/<tag> - the specific recipe is TBD)

## What is fs-dax?

Fs-dax is a means of creating a file system that resides in dax memory. A file in an fs-dax
file system is just a convenient means of accessing the subset of dax memory that is alocated
to that file. If an application opens an
fs-dax file and calls mmap() on the file descriptor, the resulting pointer provides direct
load/store access to the memory - without
ever moving data in and out of the page cache (as is the case with mmap() on "normal" files).

Posix read/write are also supported, but those are not the optimal use case for fs-dax;
read and write effectively amount to mmemcpy() in and out of the file's memory. 


# How can famfs do interesting work?

It is common in data science and AI workloads to share large datasets (e.g. data frames)
among many compute jobs that share the data. Many components in these tool chains can
memory map datasets from files. The "zero-copy formats" (e.g. Apache Arrow) are of 
particular interest because they are already oriented to formatting data sets in a way
that can be efficiently memory-mapped.

Famfs an enables a number of advantages when the compute jobs scale out:

* Large datasets can exist as one shared copy in a famfs file
* Sharded data need not be re-shuffled if the data is stored in famfs files
* When an app memory-maps a famfs file, it is directly accessing the memory;
  unlike block-based file systems, data is not read (or faulted) into local memory
  in order to be accessed

Jobs like these can be adapted to using famfs without even recompiling any components,
through a procedure like this.

1. Wrangle data into a zero-copy format
2. Copy the zero-copy files into a shared famfs file system
3. Component jobs from any node in the clluster can access the data via mmap() from the
   shared famfs file system
4. When a job is over, dismount the famfs file system and make the memory available for
   the next job...

# Famfs Requirements

1. Must create an fs-dax file system abstraction atop sharable dax memory
   (e.g. CXL Tagged Capacity)
2. Files must efficiently handle VMA faults
     - We’re exposing memory; it must run at memory speeds
     - Fast resolution of TLB & page-table faults to dax device offsets is essential
3. Must distribute metadata in a sharable way
4. Must tolerate clients with a stale copy of metadata

# Theory of operation

Famfs is a Linux file system that is administered from user space. The host device
is a dax memory device (either /dev/dax or /dev/pmem).

The file infastructure lives in the Linux kernel, which is necessary for Requirement #2
(must efficiently handle VMA faults). But the majority of the code lives in user space
and executes via the famfs cli and library.

The master node is the system that created (mkfs) a famfs file system. The sytem UUID of the
master node is stored in the superblock, and the famfs cli and library prevent client nodes
from mutating famfs metadata for a file system that they did not create.

As files and directories are allocated and created, the master adds those files to the
famfs append-only log. Clients gain visibility of files by periodically re-playing the
log.

Here are a few more details on the operation of famfs

* Only the Master node (the node that created a famfs file system) can create files.
  (It may be possible to relax or remove this limitation in future version of famfs.)
* Files are pre-allocated; famfs never does allocate-on-write
    - This means you can't ignore the fact that it is famfs when creating files
    - The famfs cli provides create and cp functions to create a fully-alllocated file,
      or to copy an existing file into famfs (respectively).
      [Click here for the cli reference](user/markdown/famfs-cli-reference.md).
* Authoritative metadata is shared as an append-only log within the shared memory. Once
  a famfs file system is mounted and the log has been played, all files are visible.
    - Replaying the log will make visible any files that were created since the last log play
* Metadata is never written back to the log; this means that in-memory metadata (inodes etc.)
  are ephemeral.
* Certain file mutations are not allowed, due to the requirement that clients with stale
  metadata must be tolerated. Some of these limitations may be relaxable in the future.
    - No delete
    - No truncate
    - No append

# What is Famfs NOT?

Famfs is not a general purpose file system, and unlike most file systems, it is not a data
storage tool. Famfs is a data *sharing* tool.

