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
#include <linux/iomap.h>

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

/* XXX: this is in dax-private.h */
struct dax_device *inode_dax(struct inode *inode);

/* TODO: move this into a list or tree hanging from superblock
 * This will be necessary to support more than one mount, and also to support more than one
 * dax device (or tag) per file system
 */
struct file *dax_filp = NULL; /* Just one global initially... */
char *dax_filename = NULL;
struct dax_device *dax_dev = NULL;
/**************/

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

static int
tagfs_meta_to_dax_offset(struct inode *inode,
			 struct iomap *iomap,
			 loff_t offset,
			 loff_t len)
{
	struct tagfs_file_meta *meta = (struct tagfs_file_meta *)inode->i_private;
	int i;
	loff_t local_offset = offset;

	for (i=0; i<meta->tfs_extent_ct; i++) {
		loff_t dax_ext_offset = meta->tfs_extents[i].offset;
		loff_t dax_ext_len    = meta->tfs_extents[i].len;

		if (local_offset < dax_ext_len) {
			iomap->offset = dax_ext_offset + local_offset;
			iomap->length = min_t(loff_t, len, (dax_ext_len - iomap->offset));
			iomap->dax_dev = dax_dev;
			return 0;
		}
		local_offset -= dax_ext_len; /* Get ready for the next extent */
	}
	return 1; /* What is correct error to return? */
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
	int alignment_errs = 0;

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

	if (!dax_filename) {
		int len = strlen(imap.daxdevname);
		struct inode *dax_inode;
		struct dax_sb_info *dax_sbinfo = NULL;

		dax_filename = kcalloc(1, len + 1, GFP_KERNEL);
		strcpy(dax_filename, imap.daxdevname);
		printk("%s: opening dax device (%s)\n", __func__, dax_filename);
		dax_filp = filp_open(dax_filename, O_RDWR, 0);
		if (!dax_filp) {
			printk(KERN_ERR "%s: failed to open dax dev %s\n", __func__,  dax_filename);
			rc = -EINVAL;
			goto errout;
		}

		/* How we get to the struct dax_device depends on whether we were given the name of
		 * a pmem device (which is block) or a dax device (which is character)
		 */
		switch (imap.extent_type) {
		case DAX_EXTENT:
			/* The supplied special-file-name is to a dax character device */
			dax_inode = file_inode(dax_filp);
			dax_dev = inode_dax(dax_inode); /* XXX Not sure yet whether this works */

			if (!dax_sbinfo) {
				printk(KERN_ERR "%s: failed to get struct dax_device from dax driver %s\n",
				       __func__, dax_filename);
				rc = -EINVAL;
				goto errout;
			}
			break;

		case FSDAX_EXTENT: {
			struct block_device *blkdev;

			/* The supplied special-file-name is to a pmem/fsdax block device */
			dax_inode = file_inode(dax_filp);
			blkdev = dax_inode->i_sb->s_bdev;
			dax_dev = fs_dax_get_by_bdev(blkdev, 0, 0 /* holder */,
						     NULL /* holder ops */);
			break;
		}
		default:
			printk("%s: unsupported extent type %d\n", __func__, imap.extent_type);
			rc = -EINVAL;
			goto errout;
			break;
		}
	} else {
		/* Dax device already open; make sure this file needs the same device.
		 * TODO: generalize to multiple devices
		 */
		if (strcmp(dax_filename, imap.daxdevname) != 0) {
			printk(KERN_ERR "%s: new dax filname (%s) differs from the first (%s)\n",
			       __func__, imap.daxdevname, dax_filename);
			rc = -EINVAL;
			goto errout;
		}
		if (!dax_dev) {
			printk(KERN_ERR "%s: dax_filename (%s) set but dax_dev is NULL\n",
			       __func__, dax_filename);
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
		printk("%s: offset %lx len %ld\n", __func__, offset, len);

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

	rc = -EINVAL;
	goto errout;

	/* Publish the tagfs metadata
	 */
	inode_lock(inode);
	if (inode->i_private) {
		rc = -EEXIST;
	} else {
		//inode->i_private = meta;
		rc = -ENXIO; /* Don't save metadata when we don't use it yet */
		i_size_write(inode, imap.file_size);
		inode->i_flags |= S_DAX;
		inode->i_private = meta;
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

static void
tagfs_get_iomap_flags_str(char *flag_str, unsigned flags)
{
	flag_str[0] = 0;

	if (flags & IOMAP_WRITE)
		strcat(flag_str, " IOMAP_WRITE");
	if (flags & IOMAP_ZERO)
		strcat(flag_str, " IOMAP_ZERO");
	if (flags & IOMAP_REPORT)
		strcat(flag_str, " IOMAP_REPORT");
	if (flags & IOMAP_FAULT)
		strcat(flag_str, " IOMAP_FAULT");
	if (flags & IOMAP_DIRECT)
		strcat(flag_str, " IOMAP_DIRECT");
	if (flags & IOMAP_NOWAIT)
		strcat(flag_str, " IOMAP_NOWAIT");
	if (flags & IOMAP_OVERWRITE_ONLY)
		strcat(flag_str, " IOMAP_OVERWRITE_ONLY");
	if (flags & IOMAP_DAX)
		strcat(flag_str, " IOMAP_DAX");
}

/**
 * tagfs-read_iomap_begin()
 *
 * This function is pretty simple because files are
 * * never partially allocated
 * * never have holes (files are never sparse)
 *
 */
static int
tagfs_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	char flag_str[200];
	int rc;

	printk("%s: offset %lld length %lld\n", __func__, offset, length);

	/* Dump flags */
	tagfs_get_iomap_flags_str(flag_str, flags);
	printk("        iomap flags: %s\n", flag_str);

	if ((offset + length) > i_size_read(inode)) {
		printk(KERN_ERR "%s: ofs + length exceeds file size; append not allowed\n", __func__);
		return -EINVAL;
	}

	/* Need to lock inode? */

	rc = tagfs_meta_to_dax_offset(inode, iomap, offset, length);

	return rc;
}

/* Should just need one set of iomap ops */
const struct iomap_ops tags_iomap_ops = {
	.iomap_begin		= tagfs_iomap_begin,
};

