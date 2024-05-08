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

#define FAMFS_KABI_VERSION 42
#define FAMFS_MAX_EXTENTS 2

/* We anticipate the possiblity of supporting additional types of extents */
enum famfs_extent_type {
	SIMPLE_DAX_EXTENT,
	INVALID_EXTENT_TYPE,
};

struct famfs_extent {
	__u64              offset;
	__u64              len;
};

enum famfs_file_type {
	FAMFS_REG,
	FAMFS_SUPERBLOCK,
	FAMFS_LOG,
};

/**
 * struct famfs_ioc_map - the famfs per-file metadata structure
 * @extent_type: what type of extents are in this ext_list
 * @file_type: Mark the superblock and log as special files. Maybe more later.
 * @file_size: Size of the file, which is <= the size of the ext_list
 * @ext_list_count: Number of extents
 * @ext_list: 1 or more extents
 */
struct famfs_ioc_map {
	enum famfs_extent_type    extent_type;
	enum famfs_file_type      file_type;
	__u64                     file_size;
	__u64                     ext_list_count;
	struct famfs_extent       ext_list[FAMFS_MAX_EXTENTS];
};

#define FAMFSIOC_MAGIC 'u'

/* famfs file ioctl opcodes */
#define FAMFSIOC_MAP_CREATE    _IOW(FAMFSIOC_MAGIC, 0x50, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GET       _IOR(FAMFSIOC_MAGIC, 0x51, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GETEXT    _IOR(FAMFSIOC_MAGIC, 0x52, struct famfs_extent)
#define FAMFSIOC_NOP           _IO(FAMFSIOC_MAGIC,  0x53)

#endif /* FAMFS_IOCTL_H */
