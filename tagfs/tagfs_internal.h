#ifndef TAGFS_INTERNAL_H
#define TAGFS_INTERNAL_H

#include "tagfs_ioctl.h"

extern const struct inode_operations tagfs_file_inode_operations;

/*
 * Each mcache map file has this hanging from its inode->i_private.
 */
struct tagfs_file_meta {
	enum extent_type              tfs_extent_type;
	size_t                        tfs_extent_ct;
	struct dax_device   *daxdev;
	struct tagfs_extent  tfs_extents[];  /* flexible array */
};

struct tagfs_mount_opts {
	umode_t mode;
};

#define TAGFS_MAX_DAXDEVS 2

extern int tagfs_blkdev_mode;
extern const struct dax_holder_operations tagfs_dax_holder_operations;
extern const struct iomap_ops tagfs_iomap_ops;
extern const struct vm_operations_struct tagfs_file_vm_ops;

struct tagfs_fs_info {
	struct mutex fsi_mutex;
	struct tagfs_mount_opts mount_opts;
	int num_dax_devs;
	dev_t dax_devno; /* XXX: ddo we need to save the devno? I think not */
	struct dax_device *dax_devp; /* TODO: indexed list of dax_devp's */
	struct block_device *bdevp;  /* TODO: indexed list of bdevp's (if usigng bdevs)
				      * (extents would index into the device list) */
};

int tagfs_file_create(struct file    *file, void __user    *arg);
void tagfs_meta_free(struct tagfs_file_meta *map);

#endif
