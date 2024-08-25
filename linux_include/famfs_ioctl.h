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

#define FAMFS_KABI_VERSION 43
#define FAMFS_MAX_EXTENTS 2
#define FAMFS_MAX_INTERLEAVED_STRIPS 16
#define FAMFS_IOC_MAX_INTERLEAVED_EXTENTS 1
#define FAMFS_IOC_MAX_INTERLEAVED_STRIPS 8

/* We anticipate the possiblity of supporting additional types of extents */
enum famfs_extent_type {
	SIMPLE_DAX_EXTENT,
	STRIPED_EXTENT,
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

/* V2 fmap structures */
enum famfs_ioc_ext_type {
	FAMFS_IOC_EXT_SIMPLE,
	FAMFS_IOC_EXT_INTERLEAVE,
};

struct famfs_ioc_simple_extent {
	__u64 devindex;
	__u64 offset;
	__u64 len;
};

struct famfs_ioc_interleaved_ext {
	__u64 ie_nstrips;
	__u64 ie_chunk_size;
	struct famfs_ioc_simple_extent *ie_strips;
};

struct famfs_ioc_fmap {
	__u64 fioc_file_size;
	enum famfs_file_type fioc_file_type;
	__u32 fioc_ext_type; /* enum famfs_log_ext_type */
	__u32 fioc_nextents;
	union {
		struct famfs_ioc_simple_extent *kse;     /* simple extent list */
		struct famfs_ioc_interleaved_ext *kie; /* interleaved ext list */
		/* will include the other extent types eventually */
	};
};

#define FAMFSIOC_MAGIC 'u'

/* famfs file ioctl opcodes */
#define FAMFSIOC_MAP_CREATE    _IOW(FAMFSIOC_MAGIC, 0x50, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GET       _IOR(FAMFSIOC_MAGIC, 0x51, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GETEXT    _IOR(FAMFSIOC_MAGIC, 0x52, struct famfs_extent)
#define FAMFSIOC_NOP           _IO(FAMFSIOC_MAGIC,  0x53)
#define FAMFSIOC_MAP_CREATE_V2 _IOW(FAMFSIOC_MAGIC, 0x54, struct famfs_ioc_fmap)
#if 0
#define FAMFSIOC_MAP_GET_V2    _IOR(FAMFSIOC_MAGIC, 0x55, struct famfs_ioc_fmap_extent)
#define FAMFSIOC_MAP_GETEXT_V2 _IOR(FAMFSIOC_MAGIC, 0x56, struct famfs_extent)
#endif
#endif /* FAMFS_IOCTL_H */
