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

int tagfs_blkdev_mode = FMODE_READ|FMODE_WRITE|FMODE_EXCL;



/* Debug stuff */
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

static int
tagfs_meta_to_dax_offset(struct inode *inode,
			 struct iomap *iomap,
			 loff_t        offset,
			 loff_t        len)
{
	struct tagfs_file_meta *meta = (struct tagfs_file_meta *)inode->i_private;
	int i;
	loff_t local_offset = offset;
	struct tagfs_fs_info  *fsi = inode->i_sb->s_fs_info;

	printk("%s: offset %llx len %lld\n", __func__, offset, len);
	for (i=0; i<meta->tfs_extent_ct; i++) {
		loff_t dax_ext_offset = meta->tfs_extents[i].offset;
		loff_t dax_ext_len    = meta->tfs_extents[i].len;

		if (local_offset < dax_ext_len) {
			iomap->offset = dax_ext_offset + local_offset;
			iomap->length = min_t(loff_t, len, (dax_ext_len - iomap->offset));
			iomap->dax_dev = fsi->dax_devp;
			printk("%s: --> daxdev offset %llx len %lld\n", __func__,
			       iomap->offset, iomap->length);
			return 0;
		}
		local_offset -= dax_ext_len; /* Get ready for the next extent */
	}
	printk("%s: Failed to resolve\n", __func__);
	return 1; /* What is correct error to return? */
}


static int
tagfs_dax_notify_failure(
	struct dax_device	*dax_devp,
	u64			offset,
	u64			len,
	int			mf_flags)
{

	printk(KERN_ERR "%s: dax_devp %llx offset %llx len %lld mf_flags %x\n",
	       __func__, (u64)dax_devp, (u64)offset, (u64)len, mf_flags);

	return -EOPNOTSUPP;
}

const struct dax_holder_operations tagfs_dax_holder_operations = {
	.notify_failure		= tagfs_dax_notify_failure,
};

/**
 * tagfs_file_create() - MCIOC_MAP_CREATE ioctl handler
 * @file:
 * @arg:        ptr to struct mcioc_map in user space
 *
 * Setup the dax mapping for a file. Files are created empty, and then function is aclled
 * (by tagfs_file_ioctl()) to setup the mapping and set the file size.
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
		printk("%s: invalid extent count %ld type %s\n",
		       __func__, ext_count, extent_type_str(imap.extent_type));
		rc = -ENOSPC;
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
	sb = inode->i_sb;
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
			printk("%s: opening dax block device (%s) by devno (%x)\n",
			       __func__, imap.devname, dax_devno);
			dax_devp = dax_dev_get(dax_devno);
			if (IS_ERR(dax_devp)) {
				rc = -PTR_ERR(dax_devp);
				goto errout;
			}
			printk("%s: da_dev %llx\n", __func__, (u64)dax_devp);
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
				printk("%p: already have block_device\n", __func__);
			} else {
				printk("%s: opening dax block device (%s)\n",
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
				printk("%s: dax_devp %llx\n", __func__, (u64)dax_devp);
				fsi->bdevp = bdevp;
				fsi->dax_devp = dax_devp;
			}
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

	/* Publish the tagfs metadata on inode->i_private */
	inode_lock(inode);
	if (inode->i_private) {
		printk("%s: inode already has i_private!\n", __func__);
		rc = -EEXIST;
	} else {
		inode->i_private = meta;
		//rc = -ENXIO; /* Don't save metadata when we don't use it yet */
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

/*********************************************************************
 * file_operations
 */

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

static unsigned long
tagfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

const char *
tagfs_get_iov_iter_type(struct iov_iter *iovi)
{
	switch (iovi->iter_type) {
	case ITER_IOVEC:    return "ITER_IOVEC";
	case ITER_KVEC:     return "ITER_KVEC";
	case ITER_BVEC:     return "ITER_BVEC";
	case ITER_PIPE:     return "ITER_PIPE";
	case ITER_XARRAY:   return "ITER_XARRAY";
	case ITER_DISCARD:  return "ITER_DISCARD";
	case ITER_UBUF:     return "ITER_UBUF";
	default:            return "ITER_INVALID";
	}
}

static noinline ssize_t
tagfs_dax_read_iter(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	ssize_t			ret = 0;


	if (!iov_iter_count(to))
		return 0; /* skip atime */

	ret = dax_iomap_rw(iocb, to, &tagfs_iomap_ops);

	file_accessed(iocb->ki_filp);
	return ret;
}

/**
 * tagfs_write_iter()
 *
 * We need our own write-iter in order to prevent append
 */
ssize_t
tagfs_dax_write_iter(struct kiocb *iocb,
		     struct iov_iter *from)
{
	struct inode		*inode = iocb->ki_filp->f_mapping->host;
	size_t count = iov_iter_count(from);
	size_t max_count = i_size_read(inode) - iocb->ki_pos;

	if (!IS_DAX(inode)) {
		printk(KERN_ERR "%s: inode %llx IS_DAX is false\n", __func__, (u64)inode);
		return 0;
	}
	/* starting offsset of write is: ioct->ki_pos
	 * length is iov_iter_count(from)  */
	/* TODO: truncate "from" if necessary so that
	 * (ki_pos + from_length) <= i_size
	 * (i.e. i_size will not increase)
	 * TODO: unit test for this
	 */
	printk("%s: iter_type=%s count %ld max_count %ldx\n",
	       __func__, tagfs_get_iov_iter_type(from), count, max_count);

	/* If write would go past EOF, truncate it to end at EOF
	 * TODO: truncate at length of extent list instead - then append can happen if sufficient
	 * pre-allocated extents exist */
	if (count > max_count) {
		printk("%s: truncating to max_count\n", __func__);
		iov_iter_truncate(from, max_count);
	}

	return dax_iomap_rw(iocb, from, &tagfs_iomap_ops);
}

const struct file_operations tagfs_file_operations = {
	/* Custom tagfs operations */
	.write_iter	   = tagfs_dax_write_iter,
	.read_iter	   = tagfs_dax_read_iter,
	.get_unmapped_area = tagfs_mmu_get_unmapped_area,
	.unlocked_ioctl    = tagfs_file_ioctl,

	/* Generic Operations */
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync, /* TODO: could to wbinv on range :-/ */
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
};

const struct inode_operations tagfs_file_inode_operations = {
	/* All generic */
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

/*********************************************************************
 * iomap_operations
 *
 * This stuff uses the iomap (dax-related) helpers to resolve file offsets to
 * offsets within a dax device.
 */
/**
 * tagfs_read_iomap_begin()
 *
 * This function is pretty simple because files are
 * * never partially allocated
 * * never have holes (never sparse)
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
		printk(KERN_ERR "%s: ofs + length exceeds file size; append not allowed\n",
		       __func__);
		return -EINVAL;
	}

	/* Need to lock inode? */

	rc = tagfs_meta_to_dax_offset(inode, iomap, offset, length);

	return rc;
}

/* Should just need one set of iomap ops */
const struct iomap_ops tagfs_iomap_ops = {
	.iomap_begin		= tagfs_iomap_begin,
};


/*********************************************************************
 * vm_operations
 */
static inline vm_fault_t
tagfs_dax_fault(
	struct vm_fault		*vmf,
	enum page_entry_size	pe_size,
	bool			write_fault,
	pfn_t			*pfn)
{
	/* We never need a special set of write_iomap_ops becuase we never have to
	 * perform allocation on write.
	 */
	return dax_iomap_fault(vmf, pe_size, pfn, NULL, &tagfs_iomap_ops);
}

static vm_fault_t
__tagfs_filemap_fault(
	struct vm_fault		*vmf,
	enum page_entry_size	pe_size,
	bool			write_fault)
{
	struct inode		*inode = file_inode(vmf->vma->vm_file);
	vm_fault_t		ret;


	if (write_fault) {
		sb_start_pagefault(inode->i_sb);
		file_update_time(vmf->vma->vm_file);
	}

	if (IS_DAX(inode)) {
		pfn_t pfn;

		ret = tagfs_dax_fault(vmf, pe_size, write_fault, &pfn);
		if (ret & VM_FAULT_NEEDDSYNC)
			ret = dax_finish_sync_fault(vmf, pe_size, pfn);
	} else {
		/* All tagfs faults will be dax... */
		printk(KERN_ERR "%s: oops, non-dax fault\n", __func__);
	}

	if (write_fault)
		sb_end_pagefault(inode->i_sb);
	return ret;
}

static inline bool
tagfs_is_write_fault(
	struct vm_fault		*vmf)
{
	printk("%s\n", __func__);
	return (vmf->flags & FAULT_FLAG_WRITE) &&
	       (vmf->vma->vm_flags & VM_SHARED);
}

static vm_fault_t
tagfs_filemap_fault(
	struct vm_fault		*vmf)
{
	printk("%s\n", __func__);
	/* DAX can shortcut the normal fault path on write faults! */
	return __tagfs_filemap_fault(vmf, PE_SIZE_PTE,
			IS_DAX(file_inode(vmf->vma->vm_file)) &&
			tagfs_is_write_fault(vmf));
}

static vm_fault_t
tagfs_filemap_huge_fault(
	struct vm_fault		*vmf,
	enum page_entry_size	pe_size)
{
	printk("%s\n", __func__);
	if (!IS_DAX(file_inode(vmf->vma->vm_file)))
		return VM_FAULT_FALLBACK;

	/* DAX can shortcut the normal fault path on write faults! */
	return __tagfs_filemap_fault(vmf, pe_size,
			tagfs_is_write_fault(vmf));
}

static vm_fault_t
tagfs_filemap_page_mkwrite(
	struct vm_fault		*vmf)
{
	printk("%s\n", __func__);
	return __tagfs_filemap_fault(vmf, PE_SIZE_PTE, true);
}

/*
 * pfn_mkwrite was originally intended to ensure we capture time stamp updates
 * on write faults. In reality, it needs to serialise against truncate and
 * prepare memory for writing so handle is as standard write fault.
 */
static vm_fault_t
tagfs_filemap_pfn_mkwrite(
	struct vm_fault		*vmf)
{

	return __tagfs_filemap_fault(vmf, PE_SIZE_PTE, true);
}

static vm_fault_t
tagfs_filemap_map_pages(
	struct vm_fault		*vmf,
	pgoff_t			start_pgoff,
	pgoff_t			end_pgoff)
{
	vm_fault_t ret;

	ret = filemap_map_pages(vmf, start_pgoff, end_pgoff);
	return ret;
}

static const struct vm_operations_struct tagfs_file_vm_ops = {
	.fault		= tagfs_filemap_fault,
	.huge_fault	= tagfs_filemap_huge_fault,
	.map_pages	= tagfs_filemap_map_pages,
	.page_mkwrite	= tagfs_filemap_page_mkwrite,
	.pfn_mkwrite	= tagfs_filemap_pfn_mkwrite,
};

