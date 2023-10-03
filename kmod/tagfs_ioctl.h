/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TAGFS_IOCTL_H
#define TAGFS_IOCTL_H

#include <linux/ioctl.h>
#include <linux/uuid.h>

#define is_aligned(POINTER, BYTE_COUNT) \
	(((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)

#define TAGFS_MAX_EXTENTS 2

enum extent_type {
	HPA_EXTENT = 13,
	DAX_EXTENT,
	FSDAX_EXTENT,
	TAG_EXTENT,
};

struct tagfs_extent {
	u64              offset;
	u64              len;
};

enum tagfs_file_type {
	TAGFS_REG,
	TAGFS_SUPERBLOCK,
	TAGFS_LOG,
};

#define TAGFS_DEVNAME_LEN 32
/**
 * struct tagfs_ioc_map
 *
 * This is the metadata that indicates where the memory is for a tagfs file
 */
struct tagfs_ioc_map {
	enum extent_type          extent_type;
	enum tagfs_file_type      file_type;
	size_t                    file_size;
	size_t                    ext_list_count;
	struct tagfs_extent      *ext_list;
};

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define TAGFSIOC_MAGIC 'u'

/* TODO: use correct _IO.... macros here */
#define TAGFSIOC_MAP_CREATE    _IOWR(TAGFSIOC_MAGIC, 1, struct tagfs_ioc_map)
#define TAGFSIOC_MAP_GET       _IOWR(TAGFSIOC_MAGIC, 2, struct tagfs_ioc_map)
#define TAGFSIOC_MAP_GETEXT    _IOWR(TAGFSIOC_MAGIC, 3, struct tagfs_extent)
#define TAGFSIOC_NOP           _IO(TAGFSIOC_MAGIC, 3)

#define TAGFSIOC_MAP_SUPERBLOCK _IOWR(TAGFSIOC_MAGIC, 4, ?)
#define TAGFSIOC_MAP_ROOTLOG    _IOWR(TAGFSIOC_MAGIC, 5, ?)


#endif /* TAGFS_IOCTL_H */
