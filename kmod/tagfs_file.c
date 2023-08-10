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

static int iomap_verbose=0;
module_param(iomap_verbose, int, 0660);

#ifndef CONFIG_MMU
#error "Tagfs requires a kernel with CONFIG_MMU enabled"
#endif

#ifndef CONFIG_DAX
#error "Tagfs requires a kernel with CONFIG_x52DAX enabled"
#endif

#ifndef CONFIG_FS_DAX
#error "Tagfs requires a kernel with CONFIG_FS_DAX enabled"
#endif

/* For GDB debug; remove later... */
#pragma GCC optimize ("O1")

int tagfs_blkdev_mode = FMODE_READ|FMODE_WRITE|FMODE_EXCL;

/* Debug stuff */

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

static void
tagfs_meta_free(
	struct tagfs_file_meta *map)
{
	kfree(map);
}

/**
 * tagfs_file_init_dax() - TAGFSIOC_MAP_CREATE ioctl handler
 * @file:
 * @arg:        ptr to struct mcioc_map in user space
 *
 * Setup the dax mapping for a file. Files are created empty, and then function is aclled
 * (by tagfs_file_ioctl()) to setup the mapping and set the file size.
 */
static int
tagfs_file_init_dax(
	struct file    *file,
	void __user    *arg)
{
	struct tagfs_file_meta *meta;
	struct tagfs_fs_info   *fsi;
	struct tagfs_ioc_map    imap;
	struct tagfs_extent    *tfs_extents = NULL;
	struct super_block     *sb;
	struct inode           *inode;

	size_t  ext_count;
	size_t  count = 0;
	int     rc = 0;
	int     i;
	int     alignment_errs = 0;

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
		printk(KERN_ERR "%s: file size %ld larger than ext list count %ld\n",
		       __func__, imap.file_size, count);
		rc = -EINVAL;
		goto errout;
	}

	rc = tagfs_meta_alloc(&meta, ext_count);
	if (rc)
		goto errout;

	meta->file_type = imap.file_type;

	if (meta->file_type == TAGFS_SUPERBLOCK)
		printk(KERN_INFO "%s: superblock\n", __func__);
	else if (meta->file_type == TAGFS_LOG)
		printk(KERN_INFO "%s: log\n", __func__);
	else
		printk(KERN_INFO "%s: Regular file\n", __func__);

	/* Fill in the internal file metadata structure */
	for (i=0; i<imap.ext_list_count; i++) {
		size_t len;
		off_t  offset;

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
		meta->tfs_extents[i].len    = len;
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

/**
 * tagfs_meta_to_dax_offset()
 *
 * This function is called for a page fault on the file (which will be limited to TLB and
 * page table faults, since the file has no backing store other than dax memory.
 *
 * Pages can be PTE (4k), PMD (2MiB) or (theoretically) PuD (1GiB)
 * (these sizes are for X86; may vary on other cpu architectures
 *
 * @inode  - the file where the fault occurred
 * @iomap  - struct iomap to be filled in to indicate where to find the right memory, relative
 *           to a dax device.
 * @offset - the offset within the file where the fault occurred (will be page boundary)
 * @len    - the length of the faulted mapping (will be a page multiple)
 * @flags
 */
static int
tagfs_meta_to_dax_offset(
	struct inode *inode,
	struct iomap *iomap,
	loff_t        offset,
	loff_t        len,
	unsigned      flags)
{
	struct tagfs_file_meta *meta = (struct tagfs_file_meta *)inode->i_private;
	int i;
	loff_t local_offset = offset;
	struct tagfs_fs_info  *fsi = inode->i_sb->s_fs_info;

	iomap->offset = offset; /* file offset */

	if (iomap_verbose) switch (meta->file_type) {
	case TAGFS_SUPERBLOCK:
		printk(KERN_NOTICE "%s: SUPERBLOCK\n", __func__);
		break;
	case TAGFS_LOG:
		printk(KERN_NOTICE "%s: LOG\n", __func__);
		break;
	case TAGFS_REG:
		printk(KERN_NOTICE "%s: REGULAR FILE\n", __func__);
		break;
	default:
		printk(KERN_ERR "%s: bad file type\n", __func__);
		break;
	}

	if (iomap_verbose)
		printk(KERN_NOTICE "%s: File offset %llx len %lld\n", __func__, offset, len);
	for (i=0; i<meta->tfs_extent_ct; i++) {
		loff_t dax_ext_offset = meta->tfs_extents[i].offset;
		loff_t dax_ext_len    = meta->tfs_extents[i].len;

		if ((dax_ext_offset == 0) && (meta->file_type != TAGFS_SUPERBLOCK))
			printk(KERN_ERR "%s: zero offset on non-superblock file!!\n", __func__);

		if (iomap_verbose)
			printk(KERN_NOTICE
			       "%s: ofs %llx len %llx tagfs: ext %d ofs %llx len %llx\n",
			       __func__, local_offset, len, i, dax_ext_offset, dax_ext_len);

		/* local_offset is the offset minus the size of extents skipped so far;
		 * If local_offset < dax_ext_len, the data of interest starts in this extent
		 */
		if (local_offset < dax_ext_len) {
			loff_t ext_len_remainder = dax_ext_len - local_offset;

			/*+
			 * OK, we found the file metadata extent where this data begins
			 * @local_offset      - The offset within the current extent
			 * @ext_len_remainder - Remaining length of ext after skipping local_offset
			 *
			 * iomap->addr is the offset within the dax device where that data
			 * starts */
			iomap->addr    = dax_ext_offset + local_offset; /* "disk offset" */
			iomap->length  = min_t(loff_t, len, ext_len_remainder);
			iomap->dax_dev = fsi->dax_devp;
			iomap->type    = IOMAP_MAPPED;
			iomap->flags   = flags;

			if (iomap_verbose)
				printk(KERN_NOTICE "%s: --> ext %d daxdev offset %llx len %lld\n",
				       __func__, i, iomap->addr, iomap->length);
			return 0;
		}
		local_offset -= dax_ext_len; /* Get ready for the next extent */
	}
	printk(KERN_ERR "%s: Failed to resolve offset %lld len %lld\n", __func__, offset, len);
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
	case TAGFSIOC_NOP:
		rc = 0;
		break;

	case TAGFSIOC_MAP_CREATE:
		rc = tagfs_file_init_dax(file, (void *)arg);
		break;

	case TAGFSIOC_MAP_GET: {
		struct inode *inode = file_inode(file);
		struct tagfs_file_meta *meta = inode->i_private;
		struct tagfs_ioc_map umeta;


		memset(&umeta, 0, sizeof(umeta));

		if (meta) {
			/* TODO: do more to harmonize these structures */
			umeta.extent_type    = meta->tfs_extent_type;
			umeta.file_size      = i_size_read(inode);
			umeta.ext_list_count = meta->tfs_extent_ct;

			rc = copy_to_user((void __user *)arg, &umeta, sizeof(umeta));
			if (rc)
				printk(KERN_NOTICE "%s: copy_to_user returned %ld\n",
				       __func__, rc);

		} else {
			rc = -EINVAL;
		}
	    }
		break;
	case TAGFSIOC_MAP_GETEXT: {
		struct inode *inode = file_inode(file);
		struct tagfs_file_meta *meta = inode->i_private;

		if (meta)
			rc = copy_to_user((void __user *)arg, meta->tfs_extents,
					  meta->tfs_extent_ct * sizeof(struct tagfs_extent));
		else
			rc = -EINVAL;
	    }
		break;
	default:
		rc = -ENOTTY;
		break;
	}

	return rc;
}

static unsigned long
tagfs_mmu_get_unmapped_area(
	struct file    *file,
	unsigned long   addr,
	unsigned long   len,
	unsigned long   pgoff,
	unsigned long   flags)
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

static ssize_t
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
tagfs_dax_write_iter(
	struct kiocb    *iocb,
	struct iov_iter *from)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	size_t max_count    = i_size_read(inode) - iocb->ki_pos;
	size_t count        = iov_iter_count(from);

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
	printk(KERN_NOTICE "%s: iter_type=%s count %ld max_count %ldx\n",
	       __func__, tagfs_get_iov_iter_type(from), count, max_count);

	/* If write would go past EOF, truncate it to end at EOF
	 * TODO: truncate at length of extent list instead - then append can happen if sufficient
	 * pre-allocated extents exist */
	if (count > max_count) {
		printk(KERN_NOTICE "%s: truncating to max_count\n", __func__);
		iov_iter_truncate(from, max_count);
	}

	return dax_iomap_rw(iocb, from, &tagfs_iomap_ops);
}

static int
tagfs_file_mmap(
	struct file		*file,
	struct vm_area_struct	*vma)
{
	struct inode		*inode = file_inode(file);

	printk(KERN_NOTICE "%s\n", __func__);
	if (!IS_DAX(inode)) {
		printk(KERN_ERR "%s: inode %llx IS_DAX is false\n", __func__, (u64)inode);
		return 0;
	}

	file_accessed(file);
	vma->vm_ops = &tagfs_file_vm_ops;
	vm_flags_set(vma, VM_HUGEPAGE);
	return 0;
}

const struct file_operations tagfs_file_operations = {
	.owner             = THIS_MODULE,

	/* Custom tagfs operations */
	.write_iter	   = tagfs_dax_write_iter,
	.read_iter	   = tagfs_dax_read_iter,
	.get_unmapped_area = tagfs_mmu_get_unmapped_area,
	.unlocked_ioctl    = tagfs_file_ioctl,
	.mmap		   = tagfs_file_mmap,

	/* Generic Operations */
	.fsync		   = noop_fsync, /* TODO: could to wbinv on range :-/ */
	.splice_read	   = generic_file_splice_read,
	.splice_write	   = iter_file_splice_write,
	.llseek		   = generic_file_llseek,
};

const struct inode_operations tagfs_file_inode_operations = {
	/* All generic */
	.setattr	   = simple_setattr,
	.getattr	   = simple_getattr,
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
 * * never "allocate on write"
 */
static int
tagfs_iomap_begin(
	struct inode	       *inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap	       *iomap,
	struct iomap	       *srcmap)
{
	char flag_str[200];
	size_t size;
	int rc;

	if (iomap_verbose)
		printk(KERN_NOTICE "%s: offset %lld length %lld\n", __func__, offset, length);

	/* Dump flags */
	if (iomap_verbose) {
		tagfs_get_iomap_flags_str(flag_str, flags);
		printk(KERN_NOTICE "        iomap flags: %s\n", flag_str);
	}

	/* TODO: find the right way to trim a write if it overflows the file's allocation
	 * This isn't quite right yet, and it's reproducible by comparing files with "cmp"
	 */
#if 0
	if ((offset + length) > i_size_read(inode)) {
		printk(KERN_ERR "%s: ofs + length exceeds file size; append not allowed\n",
		       __func__);
		return -EINVAL;
	}
#else
	/* If length overhangs i_size, truncate it to i_size */
	size = i_size_read(inode);
	if (offset > size)
		return -EINVAL;
	else
		length = min_t(size_t, length, (i_size_read(inode) - offset));
#endif

	/* Need to lock inode? */

	rc = tagfs_meta_to_dax_offset(inode, iomap, offset, length, flags);

	return rc;
}

/* Should just need one set of iomap ops */
const struct iomap_ops tagfs_iomap_ops = {
	.iomap_begin		= tagfs_iomap_begin,
};


/*********************************************************************
 * vm_operations
 *
 * Note: We never need a special set of write_iomap_ops becuase tagfs never
 * performs allocation on write.
 */

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

		ret = dax_iomap_fault(vmf, pe_size, &pfn, NULL, &tagfs_iomap_ops);
		if (ret & VM_FAULT_NEEDDSYNC)
			ret = dax_finish_sync_fault(vmf, pe_size, pfn);
	} else {
		/* All tagfs faults will be dax... */
		printk(KERN_ERR "%s: oops, non-dax fault\n", __func__);
		ret = VM_FAULT_SIGBUS;
	}

	if (write_fault)
		sb_end_pagefault(inode->i_sb);

	return ret;
}

static inline bool
tagfs_is_write_fault(
	struct vm_fault		*vmf)
{
	return (vmf->flags & FAULT_FLAG_WRITE) &&
	       (vmf->vma->vm_flags & VM_SHARED);
}

static vm_fault_t
tagfs_filemap_fault(
	struct vm_fault		*vmf)
{
	if (iomap_verbose)
		printk(KERN_NOTICE "%s\n", __func__);

	/* DAX can shortcut the normal fault path on write faults! */
	return __tagfs_filemap_fault(vmf, PE_SIZE_PTE,
			IS_DAX(file_inode(vmf->vma->vm_file)) && tagfs_is_write_fault(vmf));
}

static vm_fault_t
tagfs_filemap_huge_fault(
	struct vm_fault	       *vmf,
	enum page_entry_size	pe_size)
{
	printk(KERN_NOTICE "%s\n", __func__);

	if (!IS_DAX(file_inode(vmf->vma->vm_file))) {
		printk(KERN_ERR "%s: file not marked IS_DAX!!\n", __func__);
		return VM_FAULT_FALLBACK;
	}

	/* DAX can shortcut the normal fault path on write faults! */
	return __tagfs_filemap_fault(vmf, pe_size,
			tagfs_is_write_fault(vmf));
}

static vm_fault_t
tagfs_filemap_page_mkwrite(
	struct vm_fault		*vmf)
{
	printk(KERN_NOTICE "%s\n", __func__);
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
	printk(KERN_INFO "%s\n", __func__);
	return __tagfs_filemap_fault(vmf, PE_SIZE_PTE, true);
}

static vm_fault_t
tagfs_filemap_map_pages(
	struct vm_fault	       *vmf,
	pgoff_t			start_pgoff,
	pgoff_t			end_pgoff)
{
	vm_fault_t ret;

	if (iomap_verbose)
		printk(KERN_INFO "%s\n", __func__);

	ret = filemap_map_pages(vmf, start_pgoff, end_pgoff);
	return ret;
}

const struct vm_operations_struct tagfs_file_vm_ops = {
	.fault		= tagfs_filemap_fault,
	.huge_fault	= tagfs_filemap_huge_fault,
	.map_pages	= tagfs_filemap_map_pages,
	.page_mkwrite	= tagfs_filemap_page_mkwrite,
	.pfn_mkwrite	= tagfs_filemap_pfn_mkwrite,
};

