/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TAGFS_H
#define _LINUX_TAGFS_H

#include <linux/fs_parser.h> // bleh...

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

extern const struct fs_parameter_spec tagfs_fs_parameters[];
extern const struct file_operations tagfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;

#endif
