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
#ifndef FAMFS_INTERNAL_H
#define FAMFS_INTERNAL_H

#include "famfs_ioctl.h"

#include <linux/fs_parser.h> // bleh...
#include <linux/atomic.h>

#define FAMFS_MAGIC 0xdeadbeef

struct inode *famfs_get_inode(struct super_block *sb, const struct inode *dir,
	 umode_t mode, dev_t dev);
extern int famfs_init_fs_context(struct fs_context *fc);

#ifdef CONFIG_MMU
static inline int
famfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize)
{
	return 0;
}
#else
extern int famfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize);
#endif

extern const struct fs_parameter_spec    famfs_fs_parameters[];
extern const struct file_operations      famfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;
extern const struct inode_operations     famfs_file_inode_operations;

/*
 * Each famfs dax file has this hanging from its inode->i_private.
 */
struct famfs_file_meta {
	enum famfs_file_type  file_type;
	size_t                file_size;
	enum extent_type      tfs_extent_type;
	size_t                tfs_extent_ct;
	struct famfs_extent   tfs_extents[];  /* flexible array */
};

struct famfs_mount_opts {
	umode_t mode;
};

extern int famfs_blkdev_mode;
extern const struct iomap_ops             famfs_iomap_ops;
extern const struct vm_operations_struct  famfs_file_vm_ops;

struct famfs_fs_info {
	struct mutex             fsi_mutex;
	struct famfs_mount_opts  mount_opts;
	int                      num_dax_devs;
	struct file             *dax_filp;
	struct dax_device       *dax_devp; /* TODO: indexed list of dax_devp's */
	struct block_device     *bdevp;    /* TODO: indexed list of bdevp's (if usigng bdevs)
					    * (extents would index into the device list)
					    */
};

/*
 * filemap_fault counters
 */
enum famfs_fault {
	FAMFS_PTE = 0,
	FAMFS_PMD,
	FAMFS_PUD,
	FAMFS_NUM_FAULT_TYPES,
};

struct famfs_fault_counters {
	atomic64_t fault_ct[FAMFS_NUM_FAULT_TYPES];
};

extern struct famfs_fault_counters ffc;

static inline void famfs_clear_fault_counters(struct famfs_fault_counters *fc)
{
	int i;

	for (i = 0; i < FAMFS_NUM_FAULT_TYPES; i++)
		atomic64_set(&fc->fault_ct[i], 0);
}

static inline void famfs_inc_fault_counter(struct famfs_fault_counters *fc,
					       enum famfs_fault type)
{
	atomic64_inc(&fc->fault_ct[type]);
}

static inline u64 famfs_pte_fault_ct(struct famfs_fault_counters *fc)
{
	return atomic64_read(&fc->fault_ct[FAMFS_PTE]);
}

static inline u64 famfs_pmd_fault_ct(struct famfs_fault_counters *fc)
{
	return atomic64_read(&fc->fault_ct[FAMFS_PMD]);
}

static inline u64 famfs_pud_fault_ct(struct famfs_fault_counters *fc)
{
	return atomic64_read(&fc->fault_ct[FAMFS_PUD]);
}

#endif
