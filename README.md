
# This is the combined kernel & userspace repo for famfs.

This will eventually split into separate repos:
* A patched kernel repo with the famfs kernel component (the kmod subdirectory here)
* A userspace repo with the userspace component (the user subdirectory here)

# Background

In the coming years the emerging CXL standard will enable shared fabric-attached
memory (FAM). Famfs is intended to provide a viable usage pattern for FAM that many apps
can use without modification.

Famfs is an fs-dax file system that allows multiple hosts to mount the same file system
from the same shared memory device. The file system is administered by a Master, but can
be concurrently mounted by Clients (which default to read-only access to files, but writable
access is permitted).

Why do we need a new fs-dax file system when others (xfs, ext4) exist? Because the existing
fs-dax file systems use write-back metadata, which is not compatible with shared memory
access.

Famfs is a scale-out, shared memory file system that tolerates clients with a stale view
of metadata. 

[Famfs was introduced at the 2023 Linux Plumbers Conference](https://lpc.events/event/17/contributions/1455/). The linked page contains the abstract, plus links to the slides and a
youtube video of the talk.

# How can famfs do interesting work?

It is common in data science and AI workloads to share large datasets (e.g. data frames)
among many compute jobs that share the data. Many components in these tool chains can
memory map datasets from files. The "zero-copy formats" (e.g. Apache Arrow) are of 
particular interest because they are already oriented to formatting data sets in a way
that can be efficiently memory-mapped.

Famfs an enables a number of advantages when the compute jobs scale out:

* Large datasets can exist as one shared copy in a famfs file in shared fam
* Sharded data need not be re-shuffled if the data is stored in famfs files
* When an app memory-maps a famfs file, it is directly accessing the memory;
  unlike block-based file systems, data is not read into local memory in order to be accessed

Jobs like these can be adapted to using famfs without even recompiling any components,
through a procedure like this.

1. Wrangle data into a zero-copy format
2. Copy the zero-copy files into a shared famfs file system
3. Component jobs access the data via mmap from the shared file system

# Famfs Requirements

1. Must create an fs-dax file system abstraction atop Tagged Capacity (shared memory dax devices)
2. Files must efficiently handle VMA faults
     - We’re exposing memory; it must run at memory speeds
     - Fast resolution of TLB & page-table faults to dax device offsets is essential
3. Must distribute metadata in a sharable way
4. Must tolerate clients with a stale copy of metadata

# Theory of operation

Famfs is a kernel file system that is administered from user space. The host device
is a dax memory device (either /dev/dax or /dev/pmem). There are two key points that
explain the need for famfs.

CXL enables shared fabric-attached memory (FAM). The fs-dax pattern already exists,
but it does not work with sharable file systems because the existing fs-dax file systems
have write-back metadata. That is not shareable.

Famfs solves the sharing problem by operating as an append-only log-structured file system
with a number of key limitations.

* Only the Master node (the node that created a famfs file system) can create files
* Files are pre-allocated; famfs never does allocate-on-write
    - This means you can't ignore the fact that it is famfs when creating filels
    - The famfs cli provides create and cp functions to create a fully-alllocated file,
      or to copy an existing file into famfs (respectively).
      [Click here for the cli reference](user/markdown/famfs-cli-reference.md).
* Authoritative metadata is shared as an append-only log within the shared memory. Once
  a famfs file system is mounted and the log has been played, all files are visible.
* Metadata is never written back to the log; this means that in-memory metadata (inodes etc.)
  are ephemeral.
* Certain file mutations are not allowed, due to the requirement that clients with stale
  metadata must be tolerated. Some of these limitations may be relaxable in a future version.
    - No delete
    - No truncate
    - No append

# System Requirements

* A recent Linux distribution
* A Linux 6.5 kernel (will migrate t 6.7 soon)
* A kernel-devel package installed (or source available from custom kernel)
* Various additional packages that you may discover when you build

    Srini: when out try it out, can you send me a list of prereqs you had to install?

# Getting started

To build the software:

    make clean all

Refer to the Famfs [Userspace repo](user/README.md) for further usage info

