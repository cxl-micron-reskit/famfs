#ifndef TAGFS_IOCTL_H
#define TAGFS_IOCTL_H

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
	size_t           len;
};


/**
 * struct tagfs_ioc_map
 *
 * This is the metadata that indicates where the memory is for a tagfs file
 */
struct tagfs_ioc_map {
	enum extent_type          extent_type;
	size_t                    file_size;
	size_t                    ext_list_count;
	struct tagfs_extent      *ext_list;
	/* TODO later: move to extents, for file spanning flexibility */

	unsigned char    devname[32]; /* if DAX_EXTENT or BLOCK_EXTENT */
	dev_t            devno;
};

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define MCIOC_MAGIC 'u'

#define MCIOC_MAP_CREATE    _IOWR(MCIOC_MAGIC, 1, struct tagfs_ioc_map)
#define MCIOC_MAP_GET       _IOWR(MCIOC_MAGIC, 2, struct tagfs_ioc_map)
#define MCIOC_MAP_GETEXT    _IOWR(MCIOC_MAGIC, 3, struct tagfs_extent)

#endif /* TAGFS_IOCTL_H */
