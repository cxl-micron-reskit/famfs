/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023 Micron Technology, Inc.
 *
 * This file system, originally based on ramfs the dax support from xfs,
 * is intended to allow multiple host systems to mount a common file system
 * view of dax files that map to shared memory.
 */
#ifndef FAMFS_IOCTL_H
#define FAMFS_IOCTL_H

#include <linux/ioctl.h>
#include <linux/uuid.h>

#define FAMFS_MAX_EXTENTS 2

enum extent_type {
	SIMPLE_DAX_EXTENT = 13,
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
 * struct famfs_ioc_map
 *
 * This is the metadata that indicates where the memory is for a famfs file
 */
struct famfs_ioc_map {
	enum extent_type          extent_type;
	enum famfs_file_type      file_type;
	__u64                     file_size;
	__u64                     ext_list_count;
	struct famfs_extent       ext_list[FAMFS_MAX_EXTENTS];
};

#define FAMFSIOC_MAGIC 'u'

/* famfs file ioctl opcodes */
#define FAMFSIOC_MAP_CREATE    _IOW(FAMFSIOC_MAGIC, 1, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GET       _IOR(FAMFSIOC_MAGIC, 2, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GETEXT    _IOR(FAMFSIOC_MAGIC, 3, struct famfs_extent)
#define FAMFSIOC_NOP           _IO(FAMFSIOC_MAGIC,  4)

#endif /* FAMFS_IOCTL_H */
