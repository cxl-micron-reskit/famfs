
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


/**
 * tagfs_map_meta_alloc() - Allocate mcache map metadata
 * @mapp:       Pointer to an mcache_map_meta pointer
 * @ext_count:  The number of extents needed
 */
static
int
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
 * tagfs_file_create() - MCIOC_MAP_CREATE ioctl handler
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

	struct tagfs_user_extent *tfs_extents = NULL;
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

	/* One supported dax device, and we haven't opened it yet since we don't have the name
	 * stored yet
	 */
	if (!fsi->dax_devno) {
		//int len = strlen(imap.devname);
		//struct dax_sb_info *dax_sbinfo = NULL;

#if 0
		dax_filename = kcalloc(1, len + 1, GFP_KERNEL);
		strcpy(dax_filename, imap.devname);
#endif
		/* How we get to the struct dax_device depends on whether we were given the
		 * name of pmem device (which is block) or a dax device (which is character)
		 */
		switch (imap.extent_type) {
		case DAX_EXTENT: {
			/* Intent here is to open char device directly, and not get dax_device
			 * from a block_device, which we don't use anyway. Haven't figured it
			 * out yet though.
			 */
#if 1
			printk(KERN_ERR "%s: raw character dax device not supported yet\n",
			       __func__);
			rc = -EINVAL;
			goto errout;
#else
			printk(KERN_INFO "%s: opening dax block device (%s) by devno (%x)\n",
			       __func__, imap.devname, dax_devno);
			dax_devp = dax_dev_get(dax_devno);
			if (IS_ERR(dax_devp)) {
				rc = -PTR_ERR(dax_devp);
				goto errout;
			}
			printk(KERN_INFO "%s: da_dev %llx\n", __func__, (u64)dax_devp);
			if (!dax_sbinfo) {
				printk(KERN_ERR
				       "%s: failed to get struct dax_device from dax driver %s\n",
				       __func__, imap.devname);
				rc = -EINVAL;
				goto errout;
			}
#endif
			break;
		}
		case FSDAX_EXTENT: {
			u64 start_off = 0;
			struct block_device *bdevp;
			struct dax_device   *dax_devp;

			if (fsi->bdevp) {
				printk(KERN_NOTICE "%p: already have block_device\n", __func__);
			} else {
				printk(KERN_INFO "%s: opening dax block device (%s)\n",
				       __func__, imap.devname);

				/* TODO: open by devno instead?
				 * (less effective error checking perhaps) */
				bdevp = blkdev_get_by_path(imap.devname, tagfs_blkdev_mode, sb);
				if (IS_ERR(bdevp)) {
					rc = PTR_ERR(bdevp);
					printk(KERN_ERR "%s: failed to open block device (%s)\n"
					       , __func__, imap.devname);
					goto errout;
				}

				dax_devp = fs_dax_get_by_bdev(bdevp, &start_off,
							      sb->s_fs_info /* holder */,
							     &tagfs_dax_holder_operations);
				if (IS_ERR(dax_devp)) {
					printk(KERN_ERR "%s: unable to get daxdev from bdevp\n",
					       __func__);
					blkdev_put(bdevp, tagfs_blkdev_mode);
					goto errout;
				}
				printk(KERN_INFO "%s: dax_devp %llx\n", __func__, (u64)dax_devp);
				fsi->bdevp = bdevp;
				fsi->dax_devp = dax_devp;
			}
			break;
		}
		default:
			printk(KERN_NOTICE "%s: unsupported extent type %d\n", __func__, imap.extent_type);
			rc = -EINVAL;
			goto errout;
			break;
		}
	} else {
		/* Dax device already open; make sure this file needs the same device.
		 */
		if (fsi->dax_devno != imap.devno) {
			printk(KERN_ERR "%s: new dax devno (%x) differs from the first (%x)\n",
			       __func__, imap.devno, fsi->dax_devno);
			rc = -EINVAL;
			goto errout;
		}
		if (!fsi->dax_devp) {
			printk(KERN_ERR "%s: dax_devno (%x) set but dax_dev is NULL\n",
			       __func__, fsi->dax_devno);
			rc = -EINVAL;
			goto errout;
		}
	}

	/* Fill in the internal file metadata structure */
	for (i=0; i<imap.ext_list_count; i++) {
		size_t len;
		off_t offset;

		len    = imap.ext_list[i].len;
		offset = imap.ext_list[i].offset;

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
