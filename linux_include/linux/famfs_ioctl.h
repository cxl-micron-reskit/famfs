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
 * Extent type of a famfs file's in-core map. This is no longer part of any
 * ioctl payload (the v3 MAP_CREATE message uses fuse's enum famfs_ext_type);
 * it is retained only as the type of famfs_file_meta.fm_extent_type.
 */
enum famfs_ioc_ext_type {
	FAMFS_IOC_EXT_SIMPLE,
	FAMFS_IOC_EXT_INTERLEAVE,
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
 * MAP_CREATE (0x50, reclaimed) carries the self-describing fmap message shared
 * with the fuse GET_FMAP reply (struct fuse_famfs_fmap_header, from
 * <linux/fuse.h>). A caller that issues this opcode must include that header.
 */
#define FAMFSIOC_MAP_CREATE    _IOW(FAMFSIOC_MAGIC, 0x50, struct fuse_famfs_fmap_header)
#define FAMFSIOC_DAXDEV_OPEN   _IOW(FAMFSIOC_MAGIC, 0x56, struct famfs_ioc_daxdev)

#endif /* FAMFS_IOCTL_H */
