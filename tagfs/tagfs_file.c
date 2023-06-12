/* file-mmu.c: tagfs MMU-based file operations
 *
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/dax.h>
#include <linux/uio.h>

#include "tagfs.h"
#include "tagfs_internal.h"
#include "tagfs_ioctl.h"

MODULE_LICENSE("GPL v2");

#ifndef CONFIG_MMU
#error "Tagfs requires a kernel with CONFIG_MMU enabled"
#endif

#ifndef CONFIG_DAX
#error "Tagfs requires a kernel with CONFIG_x52DAX enabled"
#endif

#ifndef CONFIG_FS_DAX
#error "Tagfs requires a kernel with CONFIG_FS_DAX enabled"
#endif

#define MCACHE_MAP_MAX_MBLOCKS  256

/**
 * mcache_map_meta_alloc() - Allocate mcache map metadata
 * @mapp:       pointer to an mcache_map_meta pointer
 * @mbdescc:    mblock descriptor vector element count
 */
static
int
tagfs_meta_alloc(
	struct tagfs_file_meta    **mapp,
	size_t                      ext_count)
{
	struct tagfs_file_meta *map;
	size_t                  mapsz;

	*mapp = NULL;

	mapsz = sizeof(*map) + sizeof(*(map->tfs_extents)) * ext_count;

	map = kzalloc(mapsz, GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->tfs_extent_ct = ext_count;
	*mapp = map;

	return 0;
}

void
tagfs_meta_free(
	struct tagfs_file_meta *map)
{
	kfree(map);
}

/**
 * tagfs_file_create() - MCIOC_MAP_CREATE ioctl handler
 * @file:
 * @arg:        ptr to struct mcioc_map in user space
 *
 * How are mcache map files created?
 */
static
int
tagfs_file_create(
	struct file    *file,
	void __user    *arg)
{
	struct tagfs_file_meta *meta;
	struct tagfs_fs_info  *fsi;
	struct tagfs_ioc_map    imap;
	struct inode           *inode;

	struct tagfs_user_extent *tfs_extents = NULL;
	size_t  ext_count;
	int     rc;
	int     i;
	size_t  count = 0;

	tfs_extents = NULL;
	meta = NULL;

	rc = copy_from_user(&imap, arg, sizeof(imap));
	if (rc)
		return -EFAULT;

	ext_count = imap.ext_list_count;
	if (ext_count < 1) {
		printk("%s: invalid extent count %ld\n", __func__, ext_count);
		rc = -EINVAL;
		goto errout;
	}
	printk("%s: there are %ld extents\n", __func__, ext_count);

	if (ext_count > TAGFS_MAX_EXTENTS) {
		rc = -E2BIG;
		goto errout;
	}

	inode = file->f_inode;
	if (!inode) {
		printk("%s: no inode\n", __func__);
		rc = -EBADF;
		goto errout;
	}

	fsi = inode->i_sb->s_fs_info;

	/* Get space to copyin ext list from user space */
	tfs_extents = kcalloc(ext_count, sizeof(*tfs_extents), GFP_KERNEL);
	if (!tfs_extents) {
		printk("%s: Failed to alloc space for ext list\n", __func__);
		rc =-ENOMEM;
		goto errout;
	}

	rc = copy_from_user(tfs_extents, imap.ext_list,
			    ext_count * sizeof(*tfs_extents));
	if (rc) {
		printk("%s: Failed to retrieve extent list from user space\n",
		       __func__);
		rc = -EFAULT;
		goto errout;
	}

	/* Look through the extents and make sure they meet alignment reqs and
	 * add up to the right size */
	for (i=0; i<imap.ext_list_count; i++) {
		count += imap.ext_list[i].len;

		/* Each offset must be huge page aligned */
		/* TODO */
	}
	if (count != imap.file_size) {
		rc = -EINVAL;
		goto errout;
	}
	
	rc = tagfs_meta_alloc(&meta, ext_count);
	if (rc) {
		goto errout;
	}

	/* Fill in the internal file metadata structure */
	for (i=0; i<imap.ext_list_count; i++) {
		/* TODO: get HPA from Tag DAX device. Hmmm. */
		/* meta->tfs_extents[i].hpa = */
		meta->tfs_extents[i].len = imap.ext_list[i].len;
		printk("%s: addr %lx len %ld\n", __func__,
		       (ulong)imap.ext_list[i].hpa, imap.ext_list[i].len);
	}


	/* Publish the tagfs metadata
	 */
	inode_lock(inode);
	if (inode->i_private) {
		rc = -EEXIST;
	} else {
		//inode->i_private = meta;
		rc = -ENXIO; /* Don't save metadata when we don't use it yet */
		i_size_write(inode, imap.file_size);
	}
	inode_unlock(inode);
	
errout:
	if (rc)
		tagfs_meta_free(meta);

	/* This was just temporary storage: */
	if (tfs_extents)
		kfree(tfs_extents);

	return rc;
}

/**
 * tagfs_file_ioctl() -  top-level mcache ioctl handler
 * @file:
 * @cmd:
 * @arg:
 */
static
long
tagfs_file_ioctl(
	struct file    *file,
	unsigned int    cmd,
	unsigned long   arg)
{
	long rc;

	switch (cmd) {
	case MCIOC_MAP_CREATE:
		rc = tagfs_file_create(file, (void *)arg);
		break;

	default:
		rc = -ENOTTY;
		break;
	}

	return rc;
}


static unsigned long tagfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}


/**
 * tagfs_write_iter()
 *
 * We need our own write-iter in order to prevent append
 */
ssize_t
tagfs_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	/* starting offsset of write is: ioct->ki_pos
	 * 
	 */
	/* TODO: truncate "from" if necessary so that
	 * (ki_pos + from_length) <= i_size
	 * (i.e. i_size will not increase)
	 * TODO: unit test for this
	 */
	printk("%s: iter_type=%d\n", __func__, iov_iter_type(from));
	return generic_file_write_iter(iocb, from);
}

const struct file_operations tagfs_file_operations = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= tagfs_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
	.get_unmapped_area	= tagfs_mmu_get_unmapped_area,
	.unlocked_ioctl = tagfs_file_ioctl,
};

const struct inode_operations tagfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

