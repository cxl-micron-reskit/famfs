#ifndef TAGFS_INTERNAL_H
#define TAGFS_INTERNAL_H

#include "tagfs_ioctl.h"

extern const struct inode_operations tagfs_file_inode_operations;

/* This is tne internal metadata for the backing memory of a tagfs file
 */
struct tagfs_internal_extent {

	u64                 offset;
	size_t              len;
};
/*
 * Each mcache map file has this hanging from its inode->i_private.
 */
struct tagfs_file_meta {
	enum extent_type              tfs_extent_type;
	size_t                        tfs_extent_ct;
	struct dax_device   *daxdev;
	struct tagfs_internal_extent  tfs_extents[];  /* flexible array */
};

struct tagfs_mount_opts {
	umode_t mode;
};

#if 0
struct tagfs_dax_dev {
	char *daxdev_filename;
	struct block_device *bdevp;
	struct dax_device *dax_devp;
};

#define TAGFS_MAX_DAXDEVS 2
#endif

extern int tagfs_blkdev_mode;
extern const struct dax_holder_operations tagfs_dax_holder_operations;
extern const struct iomap_ops tagfs_iomap_ops;

struct tagfs_fs_info {
	struct mutex fsi_mutex;
	struct tagfs_mount_opts mount_opts;
	int num_dax_devs;
	dev_t dax_devno; /* XXX: ddo we need to save the devno? I think not */
	struct dax_device *dax_devp; /* TODO: indexed list of dax_devp's */
	struct block_device *bdevp;  /* TODO: indexed list of bdevp's (if usigng bdevs)
				      * (extents would index into the device list) */
};



#endif
