/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023-2024 Micron Technology, Inc.
 *
 * This file system, originally based on ramfs the dax support from xfs,
 * is intended to allow multiple host systems to mount a common file system
 * view of dax files that map to shared memory.
 */
#ifndef FAMFS_IOCTL_H
#define FAMFS_IOCTL_H

#include <linux/ioctl.h>
#include <linux/uuid.h>

#define FAMFS_KABI_VERSION 44

enum famfs_file_type {
	FAMFS_REG,
	FAMFS_SUPERBLOCK,
	FAMFS_LOG,
};

/*
 * Extent type in a famfs fmap message, and of the in-core map
 * (famfs_file_meta.fm_extent_type).
 */
enum famfs_ioc_ext_type {
	FAMFS_IOC_EXT_SIMPLE,
	FAMFS_IOC_EXT_INTERLEAVE,
};

/*
 * The FAMFSIOC_MAP_CREATE payload is a self-describing fmap message: a
 * struct famfs_ioc_fmap_header immediately followed by @nextents extent
 * records. @fmap_size gives the total message length, so a reader is
 * self-delimiting.
 *
 * For ext_type == FAMFS_IOC_EXT_SIMPLE the records are an array of
 * @nextents famfs_ioc_simple_ext. For ext_type == FAMFS_IOC_EXT_INTERLEAVE
 * each of the @nextents records is a famfs_ioc_iext header immediately
 * followed by ie_nstrips famfs_ioc_simple_ext strip extents.
 *
 * This wire layout is byte-identical to the fmap carried in a fuse famfs
 * GET_FMAP reply (the fuse_famfs_* structs in <linux/fuse.h>), so the same
 * userspace serializer emits both. famfs_fmap.c static_asserts that the two
 * layouts stay in lockstep.
 *
 * The message is self-describing (@fmap_size bounds it), so neither the extent
 * and strip counts nor the total size are capped by this ABI. The kernel
 * applies an internal sanity limit to the copy-in and returns -EFBIG for a
 * message larger than it will accept.
 */
#define FAMFS_FMAP_VERSION 1

struct famfs_ioc_simple_ext {
	__u32 se_devindex;
	__u32 reserved;
	__u64 se_offset;
	__u64 se_len;
};

struct famfs_ioc_iext {		/* interleaved (striped) extent */
	__u32 ie_nstrips;
	__u32 ie_chunk_size;
	__u64 ie_nbytes;	/* total bytes mapped by this interleaved extent */
	__u64 reserved;
};

struct famfs_ioc_fmap_header {
	__u8  file_type;	/* enum famfs_file_type */
	__u8  reserved;
	__u16 fmap_version;	/* FAMFS_FMAP_VERSION */
	__u32 ext_type;		/* enum famfs_ioc_ext_type */
	__u32 nextents;
	__u32 fmap_size;	/* total message bytes, including this header */
	__u64 file_size;
	__u64 reserved1;
};

/**
 * struct famfs_ioc_daxdev - register an additional backing daxdev by path
 * @daxdev_index:    the (cluster-invariant) index this daxdev occupies in
 *                   extent dev_index fields. Index 0 is the mount-time primary.
 * @daxdev_path:     userspace pointer to the devdax device path (e.g.
 *                   "/dev/dax0.0"); resolved in the kernel the same way the
 *                   mount primary is.
 * @daxdev_path_len: length of the path string, not counting the NUL.
 * @flags:           reserved; must be zero.
 *
 * Standalone famfs registers every daxdev by path: the mount primary comes in
 * as the mount device name, and slots 1..n come in here. (This deliberately
 * differs from fuse's fd-based FUSE_DEV_IOC_DAXDEV_OPEN; each side is uniform
 * within itself.) Passing the path by pointer keeps the struct fixed-size, so
 * longer paths never require an ABI change.
 */
struct famfs_ioc_daxdev {
	__u64 daxdev_index;
	__u64 daxdev_path;
	__u32 daxdev_path_len;
	__u32 flags;
};

#define FAMFSIOC_MAGIC 'u'

/* famfs file ioctl opcodes */
/* Version-agnostic */
#define FAMFSIOC_NOP           _IO(FAMFSIOC_MAGIC,  0x53)

/* ABI 44 */
/*
 * MAP_CREATE (0x50, reclaimed) carries the self-describing fmap message -
 * struct famfs_ioc_fmap_header followed by the extent list (see above).
 */
#define FAMFSIOC_MAP_CREATE    _IOW(FAMFSIOC_MAGIC, 0x50, struct famfs_ioc_fmap_header)
#define FAMFSIOC_DAXDEV_OPEN   _IOW(FAMFSIOC_MAGIC, 0x56, struct famfs_ioc_daxdev)

#endif /* FAMFS_IOCTL_H */
