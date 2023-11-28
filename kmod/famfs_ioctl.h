/* SPDX-License-Identifier: GPL-2.0 */
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

#define is_aligned(POINTER, BYTE_COUNT) \
	(((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)

#define FAMFS_MAX_EXTENTS 2

enum extent_type {
	HPA_EXTENT = 13,
	DAX_EXTENT,
	FSDAX_EXTENT,
	TAG_EXTENT,
};

struct famfs_extent {
	u64              offset;
	u64              len;
};

enum famfs_file_type {
	FAMFS_REG,
	FAMFS_SUPERBLOCK,
	FAMFS_LOG,
};

#define FAMFS_DEVNAME_LEN 32
/**
 * struct famfs_ioc_map
 *
 * This is the metadata that indicates where the memory is for a famfs file
 */
struct famfs_ioc_map {
	enum extent_type          extent_type;
	enum famfs_file_type      file_type;
	size_t                    file_size;
	size_t                    ext_list_count;
	struct famfs_extent       ext_list[FAMFS_MAX_EXTENTS];
};

static inline size_t tioc_ext_list_size(struct famfs_ioc_map *map)
{
	return (map->ext_list_count * sizeof(*map->ext_list));
}

static inline size_t tioc_ext_count(struct famfs_ioc_map *map)
{
	return map->ext_list_count;
}

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define FAMFSIOC_MAGIC 'u'

/* TODO: use correct _IO.... macros here */
#define FAMFSIOC_MAP_CREATE    _IOWR(FAMFSIOC_MAGIC, 1, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GET       _IOWR(FAMFSIOC_MAGIC, 2, struct famfs_ioc_map)
#define FAMFSIOC_MAP_GETEXT    _IOWR(FAMFSIOC_MAGIC, 3, struct famfs_extent)
#define FAMFSIOC_NOP           _IO(FAMFSIOC_MAGIC, 3)


#endif /* FAMFS_IOCTL_H */
