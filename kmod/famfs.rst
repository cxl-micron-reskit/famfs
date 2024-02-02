.. SPDX-License-Identifier: GPL-2.0

.. _famfs_index:

============================================================================
famfs: The kernel component of the famfs scale-out shared memory file system
============================================================================

- Copyright (C) 2024 Micron Technology, Inc.

Introduction
============
Compute Express Link (CXL) creates a mechanism for disaggregated or
fabric-attached memory (FAM). This poses some interesing opportunities and
challenges incases where any form of memory sharing can take place. Sharing
can be serial or concurrent - serial being that one host initializes the
memory and then another host acquires non-concurrent access. In all sharing
cases, the contents must be preserved as appropriate when hosts gain and
lose access. We use the term "shared" here to include both the sequential
and current cases.

Famfs provides a mechanism for multiple hosts to use data in shared memory,
by giving it a file system interface. With famfs, any app that understands
files (whch is all of them, right?) can access data sets in shared memory.

The famfs kernel file system is part the famfs framework; a library and cli
in user space handle metadata and direct the famfs kernel module to
instantiate files that map to specific memory.

Famfs does not attempt to solve concurrency or coherency problems for apps,
although it does solve these problems in regard to its own data structures.
Apps may encounter hard concurrency problems, but there are use cases that
are imminently useful and uncomplicated from a concurrency perspective:
serial sharing is one, and read-only concurrent sharing is another.


Principles of Operation
=======================

Without its user space components, famfs is just a clone of ramfs with added
fs-dax support. The user space components (cli, library) maintain superblocks
and logs, and use the famfs kernel component to provide a file system view of
shared fabric-attached memory across multiple hosts.

Although files are intended to be shared, each host has an independent
instance of the famfs kernel module. After mount, files are not visible until
the user space component instantiates them (normally by playing the famfs
log).

Once instantiated, files on each host can point to the same shared memory,
but metadata (inodes, etc.) are ephemeral on each host that has a mounted
instance of famfs. Like ramfs, the famfs in-kernel system is has no backing
store for metadata. If metadata is ever persisted, that must be done by the
user space components.

Famfs is Not a Conventional File System
---------------------------------------

Famfs files can be accessed by conventional means, but there are limitations.
The kernel component of famfs is not involved in space/memory allocation at
all; user space creates files and passes the allocation extent lists into the
kernel. As a practical matter files must be created via the famfs library or
cli, but they can be consumed as if they were conventional files.

Famfs differs in some important ways from conventional file systems:

* Files must be pre-allocated. Allocation is never performed on write.
  * Because of this files cannot be appended by normal means
  * Moreover, you can't properly create famfs files without using the famfs
    cli and/or user space library
* Although truncate can happen locally, it does not affect the memory
  allocation for the file.

Although famfs supports conventional (Posix) read/write, it is really intended
for mmap. If you're not using mmap, you might be missing the whole point.

The reason famfs exists is to apply the existing file system abstraction to
support shared, disaggregated or fabric-attached memory so that apps can
more easily use it.

Key Requirements
================

The primary requirements are:

1. Must support a file system abstraction backed by sharable dax memory
2. Files must efficiently handle VMA faults
3. Must support metadata distribution in a sharable way
4. Must handle clients with a stale copy of metadata

The famfs kernel component takes care of 1 and 2 above. Sharable FAM just
looks like dax memory on a host. The CXL software stack provides for memory
mapping sharable memory as dax devices. And the famfs kernel module provides
for hugetlb (PMD) fault handling in order to efficiently resolve vma faults.

Requirememnts 3 and 4 are handled by the user space components, and do not
present any new requirements for the famfs kernel component.

Requirements 3 and 4 cannot be met by conventional fs-dax file systmes (e.g.
xfs and ext4) because they use write-back metadata; it is not valid to mount
a file system on two hosts from the same in-memory metadata.

Instantiating a File
====================

Creating a file works as follows.

1. User space allocates shared memory space to back a file (or discovers the
   allocation of a file while playing the famfs log).
2. User space instantiates an empty file. At this point it is effectively a
   ramfs file.
3. User space calls the FAMFS_IOC_MAP_CREATE ioctl to provide an extent list of
   backing memory for the file. The famfs kernel module does the following:
   * Attach the extent list to the private metadata for the file.
   * Set the S_DAX flag for the inode
   * Set the size of the file
4. If it's a file create rather than a log play, the master creates a famfs
   log entry that commits the existence and allocation of the file.

At this point the file is usable and may be written, read or mmapped (depending
on permissions).

Mount options
=============

Because famfs is always empty when the kernel mount completes, it really does
not make sense to mount it read-only. That would prevent playing the log to
make the contents visible. However, two ways of achieving full or partial
read-only behavior exist:

* CXL 3.1 supports read-only access to shared memory. If the dax device backing
  a famfs instance is read-only, all files backed by that device will be read-only.
  (Metadata is technicallly still writeable, but it's also ephemeral so that's
  fine.)
* The user space components may make files read-only for some or all client
  systems. (The current default is that when the log is played, all files are
  read-only except on the system that created the file system - but this
  can be overridden on a per-file basis.)

Famfs User Space Components
===========================
As of Feb 2024 The famfs user space components are hosted here:
(TBD: CMRK famfs github link)
