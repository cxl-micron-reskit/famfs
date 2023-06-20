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

enum fname_devtype {
	FNAME_IS_BLKDEV,
	FNAME_IS_DAXDEV,
};

struct tagfs_dax_dev {
	enum fname_devtype fname_devtype;
	char *daxdev_filename;
	struct file *blkdev_filp;
	struct file *daxdev_filp;
};

#define TAGFS_MAX_DAXDEVS 2

struct tagfs_fs_info {
	struct mutex fsi_mutex;
	struct tagfs_mount_opts mount_opts;
	int num_dax_devs;
	struct tagfs_dax_dev daxdev[TAGFS_MAX_DAXDEVS];
};



#endif
