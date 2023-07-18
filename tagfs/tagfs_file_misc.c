
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/dax.h>
#include <linux/uio.h>
#include <linux/iomap.h>

#include "tagfs.h"
#include "tagfs_internal.h"
#include "tagfs_ioctl.h"
#include "tagfs_meta.h"

/* For GDB debug; remove later... */
#pragma GCC optimize ("O1")

/**
 * tagfs_map_meta_alloc() - Allocate mcache map metadata
 * @mapp:       Pointer to an mcache_map_meta pointer
 * @ext_count:  The number of extents needed
 */
static int
tagfs_meta_alloc(
	struct tagfs_file_meta  **mapp,
	size_t                    ext_count)
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

char *
extent_type_str(enum extent_type et)
{
	static char *hpa_extent   = "HPA_EXTENT";
	static char *dax_extent   = "DAX_EXTENT";
	static char *fsdax_extent = "FSDAX_EXTENT";
	static char *tag_extent   = "TAG_EXTENT";
	static char *unknown_ext  = "(Undefined extent type)";
	switch (et) {
	case HPA_EXTENT:   return hpa_extent;
	case DAX_EXTENT:   return dax_extent;
	case FSDAX_EXTENT: return fsdax_extent;
	case TAG_EXTENT:   return tag_extent;
	default:           return unknown_ext;
	}
}

/**
 * tagfs_file_create() - TAGFSIOC_MAP_CREATE ioctl handler
 * @file:
 * @arg:        ptr to struct mcioc_map in user space
 *
 * Setup the dax mapping for a file. Files are created empty, and then function is aclled
 * (by tagfs_file_ioctl()) to setup the mapping and set the file size.
 */
int
tagfs_file_create(
	struct file    *file,
	void __user    *arg)
{
	struct tagfs_file_meta *meta;
	struct tagfs_fs_info  *fsi;
	struct tagfs_ioc_map    imap;
	struct inode           *inode;
	struct super_block     *sb;

	struct tagfs_extent *tfs_extents = NULL;
	size_t  ext_count;
	int     rc = 0;
	int     i;
	size_t  count = 0;
	int alignment_errs = 0;

	tfs_extents = NULL;
	meta = NULL;

	rc = copy_from_user(&imap, arg, sizeof(imap));
	if (rc)
		return -EFAULT;

	ext_count = imap.ext_list_count;
	if (ext_count < 1) {
		printk(KERN_INFO "%s: invalid extent count %ld type %s\n",
		       __func__, ext_count, extent_type_str(imap.extent_type));
		rc = -ENOSPC;
		goto errout;
	}
	printk(KERN_INFO "%s: there are %ld extents\n", __func__, ext_count);

	if (ext_count > TAGFS_MAX_EXTENTS) {
		rc = -E2BIG;
		goto errout;
	}

	inode = file_inode(file);
	if (!inode) {
		printk(KERN_INFO "%s: no inode\n", __func__);
		rc = -EBADF;
		goto errout;
	}
	sb = inode->i_sb;
	fsi = inode->i_sb->s_fs_info;

	/* Get space to copyin ext list from user space */
	tfs_extents = kcalloc(ext_count, sizeof(*tfs_extents), GFP_KERNEL);
	if (!tfs_extents) {
		printk(KERN_INFO "%s: Failed to alloc space for ext list\n", __func__);
		rc =-ENOMEM;
		goto errout;
	}

	/* Copyin the extent list (in dax offset space) of the file */
	rc = copy_from_user(tfs_extents, imap.ext_list,
			    ext_count * sizeof(*tfs_extents));
	if (rc) {
		printk(KERN_INFO "%s: Failed to retrieve extent list from user space\n",
		       __func__);
		rc = -EFAULT;
		goto errout;
	}

	/* Look through the extents and make sure they meet alignment reqs and
	 * add up to the right size */
	for (i=0; i<imap.ext_list_count; i++)
		count += tfs_extents[i].len;

	/* File size can be <= ext list size, since extent sizes are constrained */
	if (imap.file_size > count) {
		rc = -EINVAL;
		goto errout;
	}
	/* TODO: if imap.file_size exceeds ext list count by a full extent, should at leasst warn */

	rc = tagfs_meta_alloc(&meta, ext_count);
	if (rc) {
		goto errout;
	}

	meta->file_type = imap.file_type;

	if (meta->file_type == TAGFS_SUPERBLOCK)
		printk(KERN_INFO "%s: superblock\n", __func__);
	else if (meta->file_type == TAGFS_LOG)
		printk(KERN_INFO "%s: log\n", __func__);
	else
		printk(KERN_INFO "%s: NOT superblock\n", __func__);

	/* Fill in the internal file metadata structure */
	for (i=0; i<imap.ext_list_count; i++) {
		size_t len;
		off_t offset;

		offset = imap.ext_list[i].offset;
		len    = imap.ext_list[i].len;

		printk(KERN_INFO "%s: ext %d ofs=%lx len=%lx\n", __func__, i, offset, len);

		if (offset == 0 && meta->file_type != TAGFS_SUPERBLOCK) {
			printk(KERN_ERR "%s: zero offset on non-superblock file!!\n", __func__);
			rc = -EINVAL;
			goto errout;
		}

		/* TODO: get HPA from Tag DAX device. Hmmm. */
		meta->tfs_extents[i].offset = offset;
		meta->tfs_extents[i].len = len;
		printk(KERN_INFO "%s: offset %lx len %ld\n", __func__, offset, len);

		/* All extent addresses/offsets must be 2MiB aligned,
		 * and all but the last length must be a 2MiB multiple.
		 */
		if (!is_aligned(offset, 0x200000)) {
			printk(KERN_ERR "%s: error ext %d hpa %lx not aligned\n",
			       __func__, i, offset);
			alignment_errs++;
		}
		if (i < (imap.ext_list_count - 1) && !is_aligned(len, 0x200000)) {
			printk(KERN_ERR "%s: error ext %d length %ld not aligned\n",
			       __func__, i, len);
			alignment_errs++;
		}
	}

	if (alignment_errs > 0) {
		printk(KERN_ERR "%s: there were %d alignment errors in the extent list\n",
		       __func__, alignment_errs);
		rc = -EINVAL;
	}

	/* Publish the tagfs metadata on inode->i_private */
	inode_lock(inode);
	if (inode->i_private) {
		printk(KERN_ERR "%s: inode already has i_private!\n", __func__);
		rc = -EEXIST;
	} else {
		inode->i_private = meta;
		//rc = -ENXIO; /* Don't save metadata when we don't use it yet */
		i_size_write(inode, imap.file_size);
		inode->i_flags |= S_DAX;
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
