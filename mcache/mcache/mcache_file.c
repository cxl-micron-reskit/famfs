/*
 * Copyright (C) 2015-2016 Micron Technology, Inc.  All rights reserved.
 *
 * mcache subsystem for caching mblocks from the mpool subsystem
 * based on ramfs from Linux
 */

/* file-mmu.c: ramfs MMU-based file operations
 *
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ramfs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <mse_platform/platform.h>
#include <mse_platform/uuid.h>
#include <mcache_internal.h>
#include <mcache_ioctl.h>
#include <mpctl_k_internal.h>

#define MCACHE_MAP_MAX_MBLOCKS  256

/**
 * mcache_map_meta_alloc() - Allocate mcache map metadata
 * @mapp:       pointer to an mcache_map_meta pointer
 * @mbdescc:    mblock descriptor vector element count
 */
static
int
mcache_map_meta_alloc(
	struct mcache_map_meta    **mapp,
	size_t                      mbdescc)
{
	struct mcache_map_meta *map;
	size_t                  mapsz;

	*mapp = NULL;

	mapsz = sizeof(*map) + sizeof(*(map->mcm_mbdescv)) * mbdescc;

	map = kzalloc(mapsz, GFP_KERNEL);
	if (!map)
		return ec_count(-ENOMEM);

	map->mcm_mbdescc = mbdescc;
	*mapp = map;

	return 0;
}

void
mcache_map_meta_free(
	struct mcache_map_meta *map)
{
	kfree(map);
}

/**
 * mcache_file_map_create() - MCIOC_MAP_CREATE ioctl handler
 * @file:
 * @arg:        ptr to struct mcioc_map in user space
 *
 * How are mcache map files created?
 *
 * First, a mounted instance of mcache must be associated with a mounted
 * dataset.  Initially the userland API will create an empty file in that
 * mount, and then call the MCACHE_FILE_MAP_CREATE ioctl on the file.
 * That will call mcache_file_map_create() and make it so, including
 * setting the file size.
 *
 * Notes:
 * This implementation will not notice if mblocks are the
 * same size, nor will it notice if padding after the end of
 * an mblock gets accessed. Those can be tracked, but with
 * additional overhead.
 */
static
merr_t
mcache_file_map_create(
	struct file    *file,
	void __user    *arg)
{
	struct mcache_map_meta *meta;
	struct mcache_fs_info  *fsi;
	struct mcioc_map        imap;
	struct inode           *inode;

	merr_t  err;
	u64     largest;
	u64    *mbidv;      /* mblock ID vector */
	size_t  mbidc;      /* mblock ID count */
	int     rc;
	int     i;

	mbidv = NULL;
	meta = NULL;
	err = 0;

	rc = copy_from_user(&imap, arg, sizeof(imap));
	if (rc)
		return merr_ec(EFAULT);

	mbidc = imap.im_mbidc;
	if (mbidc < 1) {
		err = merr_ec(EINVAL);
		goto errout;
	}

	if (mbidc > MCACHE_MAP_MAX_MBLOCKS) {
		err = merr_ec(E2BIG);
		goto errout;
	}

	inode = file->f_inode;
	if (!inode) {
		err = merr_ec(EBADF);
		goto errout;
	}

	fsi = inode->i_sb->s_fs_info;

	mbidv = kcalloc(mbidc, sizeof(*mbidv), GFP_KERNEL);
	if (!mbidv) {
		err = merr_ec(ENOMEM);
		goto errout;
	}

	rc = copy_from_user(mbidv, imap.im_mbidv, mbidc * sizeof(*mbidv));
	if (rc) {
		err = merr_ec(EFAULT);
		goto errout;
	}

	rc = mcache_map_meta_alloc(&meta, mbidc);
	if (rc) {
		err = merr_ec(rc);
		goto errout;
	}

	largest = 0;

	for (i = 0; i < mbidc; i++) {
		struct mblock_props props;

		/* Get mblock descriptor */
		err = mblock_lookup(fsi->fsi_mpdesc, mbidv[i], &props,
				    &meta->mcm_mbdescv[i], fsi->fsi_dsid);
		if (err) {
			ec_count();
			goto errout;
		}

		/* Sanity check properties */

		/* Determine largest mblock */
		largest = max_t(u32, largest, props.mpr_alloc_cap);
	}

	/* Must make sure largest is a page multiple */
	largest = (largest + PAGE_SIZE - 1) & PAGE_MASK;

	meta->mcm_bktsz = largest;
	imap.im_bktsz = largest;

	/* Publish the mcache meta map.
	 */
	mutex_lock(&inode->i_mutex);
	if (inode->i_private) {
		err = merr_ec(EEXIST);
	} else {
		inode->i_private = meta;
		i_size_write(inode, meta->mcm_bktsz * mbidc);
	}
	mutex_unlock(&inode->i_mutex);

errout:
	imap.im_err = err;

	if (copy_to_user(arg, &imap, sizeof(imap)))
		err = merr_ec(EFAULT);

	if (err)
		mcache_map_meta_free(meta);

	kfree(mbidv);

	return err;
}

/**
 * mcache_file_ioctl() -  top-level mcache ioctl handler
 * @file:
 * @cmd:
 * @arg:
 */
static
long
mcache_file_ioctl(
	struct file    *file,
	unsigned int    cmd,
	unsigned long   arg)
{
	merr_t  err;

	switch (cmd) {
	case MCIOC_MAP_CREATE:
		err = mcache_file_map_create(file, (void *)arg);
		break;

	default:
		err = merr_ec(ENOTTY);
		break;
	}

	return -mse_errno(err);
}

/*
 * mcache_vm_ops: same as generic_file_vm_ops, except no page_mkwrite()
 */
const struct vm_operations_struct mcache_vm_ops = {
	.fault      = filemap_fault,
	.map_pages  = filemap_map_pages,
	/* No .page_mkwrite, because mcache map files are read-only */
};

/**
 * mcache_file_mmap() -
 * @file:
 * @vma:
 *
 * Same as generic_file_mmap(), except that we need our own @mcache_vm_ops
 */
int
mcache_file_mmap(
	struct file            *file,
	struct vm_area_struct  *vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->readpage)
		return ec_count(-ENOEXEC);

	file_accessed(file);
	vma->vm_ops = &mcache_vm_ops;
	vma->vm_private_data = file->f_inode->i_private;

	return 0;
}

const struct file_operations mcache_file_operations = {
	.read_iter      = generic_file_read_iter,
	.mmap           = mcache_file_mmap,
	.splice_read    = generic_file_splice_read,
	.llseek         = generic_file_llseek,
	.unlocked_ioctl = mcache_file_ioctl,
};

const struct inode_operations mcache_file_inode_operations = {
	.setattr        = simple_setattr,
	.getattr        = simple_getattr,
};
