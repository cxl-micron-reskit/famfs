/*
 * Copyright (C) 2015-2016 Micron Technology, Inc.  All rights reserved.
 *
 * mcache subsystem for caching mblocks from the mpool subsystem
 */
#ifndef MCACHE_MCACHE_H
#define MCACHE_MCACHE_H

struct inode *
mcache_get_inode(
	struct super_block *sb,
	const struct inode *dir,
	umode_t mode,
	dev_t dev);

extern struct dentry *
mcache_mount(
	struct file_system_type    *fs_type,
	int                         flags,
	const char                 *dev_name,
	void                       *data);

extern const struct file_operations mcache_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;

#endif /* MCACHE_MCACHE_H */
