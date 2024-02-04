// SPDX-License-Identifier: GPL-2.0
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023 Micron Technology, Inc.
 *
 * This file system, originally based on ramfs the dax support from xfs,
 * is intended to allow multiple host systems to mount a common file system
 * view of dax files that map to shared memory.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/dax.h>
#include <linux/uio.h>
#include <linux/iomap.h>

#include "famfs_internal.h"
#include "famfs_ioctl.h"
#include "famfs_trace.h"

#ifndef CONFIG_MMU
#error "Famfs requires a kernel with CONFIG_MMU enabled"
#endif

#ifndef CONFIG_DAX
#error "Famfs requires a kernel with CONFIG_DAX enabled"
#endif

#ifndef CONFIG_FS_DAX
#error "Famfs requires a kernel with CONFIG_FS_DAX enabled"
#endif

/* For GDB debug; remove later... */
#pragma GCC optimize("O1")

/* blk opens are now exclusive if there is private_data */
int famfs_blkdev_mode = FMODE_READ|FMODE_WRITE;

/*
 * Basic module tuning parameters
 *
 * These appear at /sys/module/famfs/parameters
 */
static int iomap_verbose;
module_param(iomap_verbose, int, 0660);
static int famfs_verbose;
module_param(famfs_verbose, int, 0660);

/*
 * filemap_fault counters
 *
 * The counters and the fault_count_enable file live at
 * /sys/fs/famfs/
 */
struct famfs_fault_counters ffc;
static int fault_count_enable;

static ssize_t
fault_count_enable_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", fault_count_enable);
}

static ssize_t
fault_count_enable_store(struct kobject        *kobj,
			 struct kobj_attribute *attr,
			 const char            *buf,
			 size_t                 count)
{
	int value;
	int rc;

	rc = sscanf(buf, "%d", &value);
	if (rc != 1)
		return 0;

	if (value > 0) /* clear fault counters when enabling, but not when disabling */
		famfs_clear_fault_counters(&ffc);

	fault_count_enable = value;
	return count;
}

/* Individual fault counters are read-only */
static ssize_t
fault_count_pte_show(struct kobject *kobj,
		     struct kobj_attribute *attr,
		     char *buf)
{
	return sprintf(buf, "%llu", famfs_pte_fault_ct(&ffc));
}

static ssize_t
fault_count_pmd_show(struct kobject *kobj,
		     struct kobj_attribute *attr,
		     char *buf)
{
	return sprintf(buf, "%llu", famfs_pmd_fault_ct(&ffc));
}

static ssize_t
fault_count_pud_show(struct kobject *kobj,
		     struct kobj_attribute *attr,
		     char *buf)
{
	return sprintf(buf, "%llu", famfs_pud_fault_ct(&ffc));
}

static struct kobj_attribute fault_count_enable_attribute = __ATTR(fault_count_enable,
								   0660,
								   fault_count_enable_show,
								   fault_count_enable_store);
static struct kobj_attribute fault_count_pte_attribute = __ATTR(pte_fault_ct,
								0440,
								fault_count_pte_show,
								NULL);
static struct kobj_attribute fault_count_pmd_attribute = __ATTR(pmd_fault_ct,
								0440,
								fault_count_pmd_show,
								NULL);
static struct kobj_attribute fault_count_pud_attribute = __ATTR(pud_fault_ct,
								0440,
								fault_count_pud_show,
								NULL);


static struct attribute *attrs[] = {
	&fault_count_enable_attribute.attr,
	&fault_count_pte_attribute.attr,
	&fault_count_pmd_attribute.attr,
	&fault_count_pud_attribute.attr,
	NULL,
};

struct attribute_group famfs_attr_group = {
	.attrs = attrs,
};

/* End fault counters */

/* Debug stuff */

static void
famfs_get_iomap_flags_str(char *flag_str, unsigned int flags)
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
	switch (et) {
	case SIMPLE_DAX_EXTENT:   return "SIMPLE_DAX_EXTENT";
	default:           return "(Invalid extent type)";
	}
}

/**
 * famfs_map_meta_alloc() - Allocate mcache map metadata
 * @mapp:       Pointer to an mcache_map_meta pointer
 * @ext_count:  The number of extents needed
 */
static int
famfs_meta_alloc(
	struct famfs_file_meta  **metap,
	size_t                    ext_count)
{
	struct famfs_file_meta *meta;
	size_t                  metasz;

	*metap = NULL;

	metasz = sizeof(*meta) + sizeof(*(meta->tfs_extents)) * ext_count;

	meta = kzalloc(metasz, GFP_KERNEL);
	if (!meta)
		return -ENOMEM;

	meta->tfs_extent_ct = ext_count;
	*metap = meta;

	return 0;
}

static void
famfs_meta_free(
	struct famfs_file_meta *map)
{
	kfree(map);
}

static void
famfs_debug_dump_imap(struct famfs_ioc_map *imap)
{
	if (!famfs_verbose)
		return;

	pr_info("%s: ", __func__);
	switch (imap->file_type) {
	case FAMFS_SUPERBLOCK:
		pr_info(" [superblock] ");
		break;
	case FAMFS_LOG:
		pr_info(" [log file] ");
		break;
	case FAMFS_REG:
		pr_info(" [Regular file]");
		break;
	default:
		pr_err("[unrecognized file type %d]", imap->file_type);
	}

	switch (imap->extent_type) {
	case SIMPLE_DAX_EXTENT:
		pr_info(" [SIMPLE_DAX_EXTENT] ");
		break;
	default:
		pr_info(" [bogus extent type] ");
		break;
	}

	pr_info(" [size=%lld] [ext_count=%lld] [ext_list=%llx]\n",
		(u64)imap->file_size, (u64)imap->ext_list_count, (u64)imap->ext_list);
}

/**
 * famfs_file_init_dax() - FAMFSIOC_MAP_CREATE ioctl handler
 * @file:
 * @arg:        ptr to struct mcioc_map in user space
 *
 * Setup the dax mapping for a file. Files are created empty, and then function is called
 * (by famfs_file_ioctl()) to setup the mapping and set the file size.
 */
static int
famfs_file_init_dax(
	struct file    *file,
	void __user    *arg)
{
	struct famfs_file_meta *meta;
	struct famfs_fs_info   *fsi;
	struct famfs_ioc_map    imap;
	struct famfs_extent    *tfs_extents = NULL;
	struct super_block     *sb;
	struct inode           *inode;

	size_t  ext_count;
	size_t  extent_total = 0;
	int     rc = 0;
	int     i;
	int     alignment_errs = 0;

	tfs_extents = NULL;
	meta = NULL;

	rc = copy_from_user(&imap, arg, sizeof(imap));
	if (rc)
		return -EFAULT;

	famfs_debug_dump_imap(&imap);

	ext_count = imap.ext_list_count;
	if (ext_count < 1) {
		pr_err("%s: invalid extent count %ld type %s\n",
		       __func__, ext_count, extent_type_str(imap.extent_type));
		rc = -ENOSPC;
		goto errout;
	}

	if (ext_count > FAMFS_MAX_EXTENTS) {
		rc = -E2BIG;
		goto errout;
	}

	inode = file_inode(file);
	if (!inode) {
		pr_err("%s: no inode\n", __func__);
		rc = -EBADF;
		goto errout;
	}
	sb  = inode->i_sb;
	fsi = inode->i_sb->s_fs_info;

	tfs_extents = &imap.ext_list[0];

	rc = famfs_meta_alloc(&meta, ext_count);
	if (rc)
		goto errout;

	meta->file_type = imap.file_type;
	meta->file_size = imap.file_size;

	/* Fill in the internal file metadata structure */
	for (i = 0; i < imap.ext_list_count; i++) {
		size_t len;
		off_t  offset;

		offset = imap.ext_list[i].offset;
		len    = imap.ext_list[i].len;

		extent_total += len;

		if (famfs_verbose)
			pr_info("%s: ext %d ofs=%lx len=%lx\n", __func__, i, offset, len);

		if (offset == 0 && meta->file_type != FAMFS_SUPERBLOCK) {
			pr_err("%s: zero offset on non-superblock file!!\n", __func__);
			rc = -EINVAL;
			goto errout;
		}

		/* TODO: get HPA from Tag DAX device. Hmmm. */
		meta->tfs_extents[i].offset = offset;
		meta->tfs_extents[i].len    = len;

		/* All extent addresses/offsets must be 2MiB aligned,
		 * and all but the last length must be a 2MiB multiple.
		 */
		if (!is_aligned(offset, 0x200000)) {
			pr_err("%s: error ext %d hpa %lx not aligned\n",
			       __func__, i, offset);
			alignment_errs++;
		}
		if (i < (imap.ext_list_count - 1) && !is_aligned(len, 0x200000)) {
			pr_err("%s: error ext %d length %ld not aligned\n",
			       __func__, i, len);
			alignment_errs++;
		}
	}

	/*
	 * File size can be <= ext list size, since extent sizes are constrained
	 * to PMD multiples
	 */
	if (imap.file_size > extent_total) {
		pr_err("%s: file size %lld larger than ext list size %lld\n",
		       __func__, (u64)imap.file_size, (u64)extent_total);
		rc = -EINVAL;
		goto errout;
	}

	if (alignment_errs > 0) {
		pr_err("%s: there were %d alignment errors in the extent list\n",
		       __func__, alignment_errs);
		rc = -EINVAL;
	}

	/* Publish the famfs metadata on inode->i_private */
	inode_lock(inode);
	if (inode->i_private) {
		pr_err("%s: inode already has i_private!\n", __func__);
		rc = -EEXIST;
	} else {
		inode->i_private = meta;
		i_size_write(inode, imap.file_size);
		inode->i_flags |= S_DAX;
	}
	inode_unlock(inode);

 errout:
	if (rc)
		famfs_meta_free(meta);

	return rc;
}

/* XXX debug... */
const char *famfs_file_type(struct famfs_file_meta *meta)
{
	if (!meta)
		return "invalid";

	switch (meta->file_type) {
		case FAMFS_SUPERBLOCK:
			return "SUPERBLOCK";

		case FAMFS_LOG:
			return "LOG";

		case FAMFS_REG:
			return "REGULAR FILE";

	}
	return "BAD FILE TYPE";
}

/**
 * famfs_meta_to_dax_offset()
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
 *           (will be trimmed in *iomap if it's disjoint in the extent list)
 * @flags
 */
static int
famfs_meta_to_dax_offset(
	struct inode *inode,
	struct iomap *iomap,
	loff_t        offset,
	loff_t        len,
	unsigned int  flags)
{
	struct famfs_file_meta *meta = (struct famfs_file_meta *)inode->i_private;
	int i;
	loff_t local_offset = offset;
	struct famfs_fs_info  *fsi = inode->i_sb->s_fs_info;

	iomap->offset = offset; /* file offset */

	if (iomap_verbose)
		pr_notice("%s: %s\n", __func__, famfs_file_type(meta));

	if (iomap_verbose)
		pr_notice("%s: File offset %llx len %lld\n", __func__, offset, len);

	for (i = 0; i < meta->tfs_extent_ct; i++) {
		loff_t dax_ext_offset = meta->tfs_extents[i].offset;
		loff_t dax_ext_len    = meta->tfs_extents[i].len;

		if ((dax_ext_offset == 0) && (meta->file_type != FAMFS_SUPERBLOCK))
			pr_err("%s: zero offset on non-superblock file!!\n", __func__);

		if (iomap_verbose)
			pr_notice("%s: ofs %llx len %llx famfs: ext %d ofs %llx len %llx\n",
				  __func__, local_offset, len, i,
				  dax_ext_offset, dax_ext_len);

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
			 * starts
			 */
			iomap->addr    = dax_ext_offset + local_offset; /* dax dev offset */
			iomap->offset  = offset; /* file offset */
			iomap->length  = min_t(loff_t, len, ext_len_remainder);
			iomap->dax_dev = fsi->dax_devp;
			iomap->type    = IOMAP_MAPPED;
			iomap->flags   = flags;

			trace_famfs_meta_to_dax_offset(inode, offset, len,
						       iomap->addr, iomap->offset);
			if (iomap_verbose)
				pr_notice("%s: --> ext %d daxdev offset %llx len %lld\n",
				       __func__, i, iomap->addr, iomap->length);
			return 0;
		}
		local_offset -= dax_ext_len; /* Get ready for the next extent */
	}

	/*  XXX !!! set iomap to zero length in this case, and return 0 !!!
	 * This just means that the r/w is past EOF
	 */
	iomap->addr    = offset;
	iomap->offset  = offset; /* file offset */
	iomap->length  = 0; /* this had better result in no access to dax mem */
	iomap->dax_dev = fsi->dax_devp;
	iomap->type    = IOMAP_MAPPED;
	iomap->flags   = flags;

	pr_notice("%s: Access past EOF (offset %lld len %lld\n", __func__, offset, len);
	return 0;
}


/*********************************************************************
 * file_operations
 */

/**
 * famfs_file_ioctl() -  top-level mcache ioctl handler
 * @file:
 * @cmd:
 * @arg:
 */
static
long
famfs_file_ioctl(
	struct file    *file,
	unsigned int    cmd,
	unsigned long   arg)
{
	long rc;

	switch (cmd) {
	case FAMFSIOC_NOP:
		rc = 0;
		break;

	case FAMFSIOC_MAP_CREATE:
		rc = famfs_file_init_dax(file, (void *)arg);
		break;

	case FAMFSIOC_MAP_GET: {
		struct inode *inode = file_inode(file);
		struct famfs_file_meta *meta = inode->i_private;
		struct famfs_ioc_map umeta;

		memset(&umeta, 0, sizeof(umeta));

		if (meta) {
			/* TODO: do more to harmonize these structures */
			umeta.extent_type    = meta->tfs_extent_type;
			umeta.file_size      = i_size_read(inode);
			umeta.ext_list_count = meta->tfs_extent_ct;

			rc = copy_to_user((void __user *)arg, &umeta, sizeof(umeta));
			if (rc)
				pr_err("%s: copy_to_user returned %ld\n", __func__, rc);

		} else {
			rc = -EINVAL;
		}
	}
		break;
	case FAMFSIOC_MAP_GETEXT: {
		struct inode *inode = file_inode(file);
		struct famfs_file_meta *meta = inode->i_private;

		if (meta)
			rc = copy_to_user((void __user *)arg, meta->tfs_extents,
					  meta->tfs_extent_ct * sizeof(struct famfs_extent));
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

const char *
famfs_get_iov_iter_type(struct iov_iter *iovi)
{
	switch (iovi->iter_type) {
	case ITER_IOVEC:    return "ITER_IOVEC";
	case ITER_KVEC:     return "ITER_KVEC";
	case ITER_BVEC:     return "ITER_BVEC";
	case ITER_XARRAY:   return "ITER_XARRAY";
	case ITER_DISCARD:  return "ITER_DISCARD";
	case ITER_UBUF:     return "ITER_UBUF";
	default:            return "ITER_INVALID";
	}
}

static ssize_t
famfs_dax_read_iter(
	struct kiocb		*iocb,
	struct iov_iter		*to)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	size_t i_size       = i_size_read(inode);
	ssize_t			ret = 0;
	size_t count        = iov_iter_count(to);
	struct famfs_file_meta *meta = inode->i_private;
	size_t max_count;

	if (famfs_verbose)
		pr_info("%s: ofs %lld count %ld type %s i_size %ld\n", __func__,
			iocb->ki_pos, iov_iter_count(to), famfs_get_iov_iter_type(to), i_size);

	if (!meta) {
		pr_err("%s: un-initialized famfs file\n", __func__);
		return -EIO;
	}
	if (i_size != meta->file_size) {
		pr_err("%s: something changed the size from  %ld to %ld\n",
		       __func__, meta->file_size, i_size);
		return -ENXIO;
	}
	if (!IS_DAX(inode)) {
		pr_err("%s: inode %llx IS_DAX is false\n", __func__, (u64)inode);
		return 0;
	}

	max_count = max_t(size_t, 0, i_size - iocb->ki_pos);

	if (count > max_count) {
		if (famfs_verbose)
			pr_notice("%s: truncating to max_count\n", __func__);
		iov_iter_truncate(to, max_count);
	}

	if (!iov_iter_count(to))
		return 0; /* skip atime */

	if (iomap_verbose)
		pr_notice("%s: ki_pos=%llx\n", __func__, (u64)iocb->ki_pos);

	ret = dax_iomap_rw(iocb, to, &famfs_iomap_ops);

	file_accessed(iocb->ki_filp);
	return ret;
}

/**
 * famfs_write_iter()
 *
 * We need our own write-iter in order to prevent append
 */
ssize_t
famfs_dax_write_iter(
	struct kiocb    *iocb,
	struct iov_iter *from)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	size_t i_size       = i_size_read(inode);
	size_t count        = iov_iter_count(from);
	struct famfs_file_meta *meta = inode->i_private;
	size_t max_count;

	if (!meta) {
		pr_err("%s: un-initialized famfs file\n", __func__);
		return -EIO;
	}
	if (i_size != meta->file_size) {
		pr_err("%s: something changed the size from  %ld to %ld\n",
		       __func__, meta->file_size, i_size);
		return -ENXIO;
	}
	if (!IS_DAX(inode)) {
		pr_err("%s: inode %llx IS_DAX is false\n", __func__, (u64)inode);
		return 0;
	}

	max_count = max_t(size_t, 0, i_size - iocb->ki_pos);

	/* Starting offset of write is: ioct->ki_pos
	 * length is iov_iter_count(from)
	 */

	if (famfs_verbose)
		pr_notice("%s: iter_type=%s offset %lld count %ld max_count %ldx\n",
			  __func__, famfs_get_iov_iter_type(from), iocb->ki_pos, count, max_count);

	/* If write would go past EOF, truncate it to end at EOF
	 * TODO: truncate at length of extent list instead - then append can happen if sufficient
	 * pre-allocated extents exist
	 */
	if (count > max_count) {
		if (famfs_verbose)
			pr_notice("%s: truncating to max_count\n", __func__);
		iov_iter_truncate(from, max_count);
	}

	if (!iov_iter_count(from))
		return 0; /* skip atime */

	if (iomap_verbose)
		pr_notice("%s: ki_pos=%llx\n", __func__, (u64)iocb->ki_pos);

	return dax_iomap_rw(iocb, from, &famfs_iomap_ops);
}

static int
famfs_file_mmap(
	struct file		*file,
	struct vm_area_struct	*vma)
{
	struct inode		*inode = file_inode(file);
	struct famfs_file_meta *meta = (struct famfs_file_meta *)inode->i_private;

	if (famfs_verbose)
		pr_notice("%s(%s)\n", __func__, famfs_file_type(meta));

	if (!IS_DAX(inode)) {
		pr_err("%s: inode %llx IS_DAX is false\n", __func__, (u64)inode);
		return 0;
	}

	file_accessed(file);
	vma->vm_ops = &famfs_file_vm_ops;
	vm_flags_set(vma, VM_HUGEPAGE);
	return 0;
}

/* Wrappers for generic functions, we can see them being called */
ssize_t famfs_file_splice_read(struct file *in, loff_t *ppos,
			       struct pipe_inode_info *pipe, size_t len,
			       unsigned int flags)
{
	ssize_t rc;

	if (famfs_verbose) {
		struct inode		*inode = file_inode(in);
		struct famfs_file_meta *meta = (struct famfs_file_meta *)inode->i_private;

		pr_info("%s(%s): ppos %lld len %ld flags %x\n",
			__func__, famfs_file_type(meta), *ppos, len, flags);
	}

	rc = filemap_splice_read(in, ppos, pipe, len, flags);
	if (famfs_verbose)
		pr_info("%s: rc %ld\n", __func__, rc);
	return rc;
}

ssize_t
famfs_iter_file_splice_write(struct pipe_inode_info *pipe, struct file *out,
			     loff_t *ppos, size_t len, unsigned int flags)
{
	ssize_t rc;

	if (famfs_verbose) {
		struct inode		*inode = file_inode(out);
		struct famfs_file_meta *meta = (struct famfs_file_meta *)inode->i_private;

		pr_info("%s(%s): ppos %lld len %ld flags %x\n",
			__func__, famfs_file_type(meta), *ppos, len, flags);
	}
	rc = iter_file_splice_write(pipe, out, ppos, len, flags);

	if (famfs_verbose)
		pr_info("%s: rc %ld\n", __func__, rc);
	return rc;
}

loff_t famfs_generic_file_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t rc;

	if (famfs_verbose)
		pr_info("%s: offset %lld whence %d\n", __func__, offset, whence);

	rc = generic_file_llseek(file, offset, whence);

	if (famfs_verbose)
		pr_info("%s: rc %lld\n", __func__, rc);
	return rc;

}

const struct file_operations famfs_file_operations = {
	.owner             = THIS_MODULE,

	/* Custom famfs operations */
	.write_iter	   = famfs_dax_write_iter,
	.read_iter	   = famfs_dax_read_iter,
	.unlocked_ioctl    = famfs_file_ioctl,
	.mmap		   = famfs_file_mmap,

	/*
	 * Note: drivers/dax/device.c:dax_get_unmapped_area() is a pattern
	 * that would support 1GiB pages. This would make sense if the allocation unit
	 * could be set to 1GiB
	 */
	.get_unmapped_area = thp_get_unmapped_area, /* thp_get_unmapped_area() will guarantee
						     * PMD page alignment, which guarantees PMD
						     * faults (rather than PTE) in most cases
						     */

	/* Generic Operations */
	.fsync		   = noop_fsync, /* TODO: could to wbinv on range :-/ */

	/* XXX: these can probably return to the generic versions */
	.splice_read	   = famfs_file_splice_read,
	.splice_write	   = famfs_iter_file_splice_write,
	.llseek		   = famfs_generic_file_llseek,
};

const struct inode_operations famfs_file_inode_operations = {
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
 * famfs_iomap_begin()
 *
 * This function is pretty simple because files are
 * * never partially allocated
 * * never have holes (never sparse)
 * * never "allocate on write"
 */
static int
famfs_iomap_begin(
	struct inode	       *inode,
	loff_t			offset,
	loff_t			length,
	unsigned int		flags,
	struct iomap	       *iomap,
	struct iomap	       *srcmap)
{
	struct famfs_file_meta *meta = inode->i_private;
	char flag_str[200];
	size_t size;
	int rc;

	/* Dump flags */
	if (iomap_verbose) {
		struct famfs_file_meta *meta = (struct famfs_file_meta *)inode->i_private;

		pr_notice("%s(%s): offset %lld length %lld\n",
			  __func__, famfs_file_type(meta),  offset, length);

		famfs_get_iomap_flags_str(flag_str, flags);
		pr_notice("        iomap flags: %s\n", flag_str);
	}

	size = i_size_read(inode);

	if (size != meta->file_size)  /* Temporary for debug */
		pr_err("%s: something changed the size from  %ld to %ld\n",
		       __func__, meta->file_size, size);

	/* Need to lock inode? */

	rc = famfs_meta_to_dax_offset(inode, iomap, offset, length, flags);

	return rc;
}

/* Should just need one set of iomap ops */
const struct iomap_ops famfs_iomap_ops = {
	.iomap_begin		= famfs_iomap_begin,
};


/*********************************************************************
 * vm_operations
 *
 * Note: We never need a special set of write_iomap_ops becuase famfs never
 * performs allocation on write.
 */

static vm_fault_t
__famfs_filemap_fault(
	struct vm_fault		*vmf,
#ifndef K67
	enum page_entry_size	pe_size,
#else
	unsigned int	pe_size,
#endif
	bool			write_fault)
{
	struct inode		*inode = file_inode(vmf->vma->vm_file);
	vm_fault_t		ret;

	trace_famfs_filemap_fault(inode, pe_size, write_fault);

	if (write_fault) {
		sb_start_pagefault(inode->i_sb);
		file_update_time(vmf->vma->vm_file);
	}

	if (IS_DAX(inode)) {
		pfn_t pfn;

		if (fault_count_enable)
			famfs_inc_fault_counter(&ffc, pe_size);

		if (iomap_verbose)
			pr_notice("%s: pgoff=%llx\n", __func__, (u64)vmf->pgoff);

		ret = dax_iomap_fault(vmf, pe_size, &pfn, NULL, &famfs_iomap_ops);
		if (ret & VM_FAULT_NEEDDSYNC)
			ret = dax_finish_sync_fault(vmf, pe_size, pfn);
	} else {
		/* All famfs faults will be dax... */
		pr_err("%s: oops, non-dax fault\n", __func__);
		ret = VM_FAULT_SIGBUS;
	}

	if (write_fault)
		sb_end_pagefault(inode->i_sb);

	return ret;
}

static inline bool
famfs_is_write_fault(
	struct vm_fault		*vmf)
{
	return (vmf->flags & FAULT_FLAG_WRITE) &&
	       (vmf->vma->vm_flags & VM_SHARED);
}

static vm_fault_t
famfs_filemap_fault(
	struct vm_fault		*vmf)
{
	if (iomap_verbose)
		pr_notice("%s pgoff %ld\n", __func__, vmf->pgoff);

	/* DAX can shortcut the normal fault path on write faults! */
	return __famfs_filemap_fault(vmf, 0,
			IS_DAX(file_inode(vmf->vma->vm_file)) && famfs_is_write_fault(vmf));
}

static vm_fault_t
famfs_filemap_huge_fault(
	struct vm_fault	       *vmf,
#ifndef K67
	enum page_entry_size	pe_size)
#else
	unsigned int	pe_size)
#endif
{
	if (famfs_verbose)
		pr_notice("%s pgoff %ld\n", __func__, vmf->pgoff);

	if (!IS_DAX(file_inode(vmf->vma->vm_file))) {
		pr_err("%s: file not marked IS_DAX!!\n", __func__);
		return VM_FAULT_FALLBACK;
	}

	/* DAX can shortcut the normal fault path on write faults! */
	return __famfs_filemap_fault(vmf, pe_size,
			famfs_is_write_fault(vmf));
}

static vm_fault_t
famfs_filemap_page_mkwrite(
	struct vm_fault		*vmf)
{
	if (famfs_verbose)
		pr_notice("%s\n", __func__);

	return __famfs_filemap_fault(vmf, 0, true);
}

/*
 * pfn_mkwrite was originally intended to ensure we capture time stamp updates
 * on write faults. In reality, it needs to serialise against truncate and
 * prepare memory for writing so handle is as standard write fault.
 */
static vm_fault_t
famfs_filemap_pfn_mkwrite(
	struct vm_fault		*vmf)
{
	if (famfs_verbose)
		pr_info("%s\n", __func__);

	return __famfs_filemap_fault(vmf, 0, true);
}

static vm_fault_t
famfs_filemap_map_pages(
	struct vm_fault	       *vmf,
	pgoff_t			start_pgoff,
	pgoff_t			end_pgoff)
{
	vm_fault_t ret;

	if (iomap_verbose)
		pr_info("%s\n", __func__);

	ret = filemap_map_pages(vmf, start_pgoff, end_pgoff);
	return ret;
}

const struct vm_operations_struct famfs_file_vm_ops = {
	.fault		= famfs_filemap_fault,
	.huge_fault	= famfs_filemap_huge_fault,
	.map_pages	= famfs_filemap_map_pages,
	.page_mkwrite	= famfs_filemap_page_mkwrite,
	.pfn_mkwrite	= famfs_filemap_pfn_mkwrite,
};

