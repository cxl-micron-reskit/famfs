.. SPDX-License-Identifier: GPL-2.0

.. _famfs_index:

============================================================================
famfs: The kernel component of the famfs scale-out shared memory file system
============================================================================

- Copyright (C) 2024 Micron Technology, Inc.

Introduction
============
Compute Express Link (CXL) creates a mechanism for disaggregated memory. This
poses some interesing challenges in cases where any form of memory sharing can
take place. Sharing can be serial or concurrent - serial being that one host
initializes the memory and then another host acquires non-concurrent access.
In all sharing cases, the contents must be preserved as appropriate when
hosts gain and lose access. We use the term "shared" here to include both the
sequential and current cases.

Famfs creates a mechanism for multiple hosts to use data in shared memory,
by giving it a file system interface. With famfs, any app that understands
files (whch is all of them, right?) can access data sets in shared memory.

The famfs kernel file system is part the famfs framework that provides a
scale-out file-oriented interface to shared fabric-attached memory (FAM).
The kernel component of famfs is based on ramfs, with added fs-dax suport.
A famfs file system is administered from user space by the libfamfs framework.

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

Although files are intended to be shared, each host has an independent instance
of the famfs kernel module. After mount, files are not instantiated until the
user space component instantiates them (normally by playing the famfs log).

Once instantiated, files on each host can point to the same shared memory,
but metadata (inodes, etc.) are ephemeral on each host that has a mounted
instance of famfs. Like ramfs, the famfs in-kernel system is has no backing
store for metadata. If metadata is ever persisted, that must be done by the
user space components.

Famfs is Not a Conventional File System
---------------------------------------

Famfs files can be accessed by conventional means, but there are limitations.
The kernel component of famfs is not involved in space/memory allocation at
all; user space creates files and passes the allocationextent lists into the
kernel. As a practical matter files must be created via the famfs library or
cli, but they can be consumed as if they were conventional files.

Famfs differs in some important ways from conventional file systems:

* Files must be pre-allocated. Allocation is never performed on write.
  * Because of this files cannot be appended by normal means
  * Moreover, you can't properly create famfs files withoout using the famfs
    cli and/or user space library
* Although truncate can happen locally, it does not affect the memory
  alllocation for the file.

Although famfs supports conventional (Posix) read/write, it is really intended
for mmap. If you're not using mmap, you might be missing the whole point.


Key Requirements
================
The reason famfs exists is to support shared fabric-attached memory. 

1. Must support a file system abstraction backed by sharable dax memory
2. Files must efficiently handle VMA faults
3. Must support metadata distribution in a sharable way
4. Must tolerate clients with a stale copy of metadata

The famfs kernel component takes care of 1 and 2 above. Sharable FAM just
looks like dax memory on a host. The CXL 3 software stack provides for memory
allocations to have unique tags (UUIDs). And the famfs kernel module provides
for hugetlb (PMD) fault handling in order to efficiently resolve vma faults.

Requirememnts 3 and 4 are handled by the user space components, and do not
present any new requirements for the famfs kernel component.


Instantiating a File
====================

Creating a file works as follows.

1. User space allocates shared memory space to back a file
2. User space instantiates an empty file. At this point it is effectively a
   ramfs file.
3. User space calls the FAMFS_IOC_MAP_CREATE ioctl to provide an extent list of
   backing memory for the file. The famfs kernel module does the following:
   * Attach the extent list to the private metadata for the file.
   * Set the S_DAX flag for the inode
   * Set the size of the file

At this point the file us usable and may be written, read or mmapped (provided
sufficient permissions).

Mount options
=============

Because famfs is always empty when the kernel mount completes, it really does
not make sense to mount it read-only. That would prevent playing the log to
make the contents visible. However, two ways of achieving full or partial
read-only behavior exist:

* CXL supports read-only access to shared memory. If the dax device backing a
  file is read-only, all files backed by that device will be read-only.
  (Metadata is technicallly still writeable, but it's also ephemeral so that's
  fine.)
* The user space components may make files read-only for some or all client
  systems. (The current default is that when the log is played, all files are
  read-only except on the system that created the file system - but this
  can be overridden on a per-file basis.)

Atime is not supported.
