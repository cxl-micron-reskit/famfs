#ifndef TAGFS_INTERNAL_H
#define TAGFS_INTERNAL_H

#include "tagfs_ioctl.h"

extern const struct inode_operations tagfs_file_inode_operations;

/* This is tne internal metadata for the backing memory of a tagfs file
 */
struct tagfs_internal_extent {
	struct block_device *blkdev;
	struct dax_device   *daxdev;
	u64                 offset;
	size_t              len;
};
/*
 * Each mcache map file has this hanging from its inode->i_private.
 */
struct tagfs_file_meta {
	enum extent_type              tfs_extent_type;
	size_t                        tfs_extent_ct;
	struct tagfs_internal_extent  tfs_extents[];  /* flexible array */
};


#endif
