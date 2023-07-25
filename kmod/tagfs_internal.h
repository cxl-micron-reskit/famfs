#ifndef TAGFS_INTERNAL_H
#define TAGFS_INTERNAL_H

#include "tagfs_ioctl.h"

#include <linux/fs_parser.h> // bleh...

#define TAGFS_MAGIC 0xdeadbeef

struct inode *tagfs_get_inode(struct super_block *sb, const struct inode *dir,
	 umode_t mode, dev_t dev);
extern int tagfs_init_fs_context(struct fs_context *fc);

#ifdef CONFIG_MMU
static inline int
tagfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize)
{
	return 0;
}
#else
extern int tagfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize);
#endif

extern const struct fs_parameter_spec    tagfs_fs_parameters[];
extern const struct file_operations      tagfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;
extern const struct inode_operations     tagfs_file_inode_operations;

/*
 * Each mcache map file has this hanging from its inode->i_private.
 */
struct tagfs_file_meta {
	enum extent_type      tfs_extent_type;
	enum tagfs_file_type  file_type;
	size_t                tfs_extent_ct;
	char                 *dax_devname;
	struct dax_device    *daxdev;
	struct tagfs_extent   tfs_extents[];  /* flexible array */
};

struct tagfs_mount_opts {
	umode_t mode;
};

extern int tagfs_blkdev_mode;
extern const struct dax_holder_operations tagfs_dax_holder_operations;
extern const struct iomap_ops             tagfs_iomap_ops;
extern const struct vm_operations_struct  tagfs_file_vm_ops;

struct tagfs_fs_info {
	struct mutex             fsi_mutex;
	struct tagfs_mount_opts  mount_opts;
	int                      num_dax_devs;
	char                    *root_daxdev;
	struct dax_device       *dax_devp; /* TODO: indexed list of dax_devp's */
	struct block_device     *bdevp;    /* TODO: indexed list of bdevp's (if usigng bdevs)
					   * (extents would index into the device list) */
};

#endif
