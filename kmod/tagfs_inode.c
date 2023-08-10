/*
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

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/seq_file.h>
#include <linux/dax.h>
#include <linux/hugetlb.h>
#include <linux/uio.h>
#include <linux/iomap.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/pfn_t.h>
#include <linux/dax.h>

#include "tagfs.h"
#include "tagfs_internal.h"

/* Because this is exported but only prototyped in dax-private.h: */
struct dax_device *inode_dax(struct inode *inode);

#define TAGFS_DEFAULT_MODE	0755

static const struct super_operations tagfs_ops;
static const struct inode_operations tagfs_dir_inode_operations;

struct inode *tagfs_get_inode(
	struct super_block *sb,
	const struct inode *dir,
	umode_t             mode,
	dev_t               dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
		inode->i_mapping->a_ops = &ram_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &tagfs_file_inode_operations;
			inode->i_fop = &tagfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &tagfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
tagfs_mknod(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	umode_t           mode,
	dev_t             dev)
{
	struct inode *inode = tagfs_get_inode(dir->i_sb, dir, mode, dev);
	int error           = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

static int tagfs_mkdir(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	umode_t           mode)
{
	int retval = tagfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);

	if (!retval)
		inc_nlink(dir);

	return retval;
}

static int tagfs_create(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	umode_t           mode,
	bool              excl)
{
	return tagfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int tagfs_symlink(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	const char       *symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = tagfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = current_time(dir);
		} else
			iput(inode);
	}
	return error;
}

static int tagfs_tmpfile(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct file      *file,
	umode_t           mode)
{
	struct inode *inode;

	inode = tagfs_get_inode(dir->i_sb, dir, mode, 0);
	if (!inode)
		return -ENOSPC;

	d_tmpfile(file, inode);
	return finish_open_simple(file, 0);
}

static const struct inode_operations tagfs_dir_inode_operations = {
	.create		= tagfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= tagfs_symlink,
	.mkdir		= tagfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= tagfs_mknod,
	.rename		= simple_rename,
	.tmpfile	= tagfs_tmpfile,
};

/*
 * Display the mount options in /proc/mounts.
 */
static int tagfs_show_options(
	struct seq_file *m,
	struct dentry   *root)
{
	struct tagfs_fs_info *fsi = root->d_sb->s_fs_info;

	if (fsi->mount_opts.mode != TAGFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mount_opts.mode);

	return 0;
}

static const struct super_operations tagfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= tagfs_show_options,
};

enum tagfs_param {
	Opt_mode,
	Opt_dax,
	Opt_rootdev,
	Opt_daxdev,
};

const struct fs_parameter_spec tagfs_fs_parameters[] = {
	fsparam_u32oct("mode",	  Opt_mode),
	fsparam_string("dax",     Opt_dax),
	fsparam_string("rootdev", Opt_rootdev),
	fsparam_string("daxdev",  Opt_daxdev),
	{}
};

static int tagfs_parse_param(
	struct fs_context   *fc,
	struct fs_parameter *param)
{
	struct tagfs_fs_info *fsi = fc->s_fs_info;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, tagfs_fs_parameters, param, &result);
	if (opt == -ENOPARAM) {
		opt = vfs_parse_fs_param_source(fc, param);
		if (opt != -ENOPARAM)
			return opt;
		/*
		 * We might like to report bad mount options here;
		 * but traditionally tagfs has ignored all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to ignore other mount options.
		 */
		return 0;
	}
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_mode:
		fsi->mount_opts.mode = result.uint_32 & S_IALLUGO;
		break;
	case Opt_dax:
		if (strcmp(param->string, "always"))
			printk(KERN_NOTICE "%s: invalid dax mode %s\n",
			       __func__, param->string);
		break;
	}

	return 0;
}

/**************************************************************************************/

#if 0
/* From struct pmem_device */
struct tagfs_device {
	/* One contiguous memory region per device */
	phys_addr_t		phys_addr;
	/* when non-zero this device is hosting a 'pfn' instance */
	phys_addr_t		data_offset;
	u64			pfn_flags;
	void			*virt_addr;
	/* immutable base size of the namespace */
	size_t			size;
#if 0
	/* trim size when namespace capacity has been section aligned */
	u32			pfn_pad;
	struct kernfs_node	*bb_state;
	struct badblocks	bb;
	struct dax_device	*dax_dev;
	struct gendisk		*disk;
	struct dev_pagemap	pgmap;
#endif
};

/**
 * @tagfs_dax_operations
 */
long
__tagfs_direct_access(
	struct tagfs_device   *tagfs_dev,
	pgoff_t                pgoff,
	long                   nr_pages,
	enum dax_access_mode   mode,
	void                 **kaddr,
	pfn_t                 *pfn)
{
	resource_size_t offset = PFN_PHYS(pgoff) + tagfs_dev->data_offset;
	//struct badblocks *bb = &tagfs_dev->bb;

	if (kaddr)
		*kaddr = tagfs_dev->virt_addr + offset;
	if (pfn)
		*pfn = phys_to_pfn_t(tagfs_dev->phys_addr + offset, tagfs_dev->pfn_flags);

#if 0
	if (bb->count &&
	    badblocks_check(bb, sector, num, &first_bad, &num_bad)) {
		long actual_nr;

		if (mode != DAX_RECOVERY_WRITE)
			return -EIO;

		/*
		 * Set the recovery stride is set to kernel page size because
		 * the underlying driver and firmware clear poison functions
		 * don't appear to handle large chunk(such as 2MiB) reliably.
		 */
		actual_nr = PHYS_PFN(
			PAGE_ALIGN((first_bad - sector) << SECTOR_SHIFT));
		dev_dbg(tagfs_dev->bb.dev,
			"start sector(%llu), nr_pages(%ld), first_bad(%llu), actual_nr(%ld)\n",
			sector, nr_pages, first_bad, actual_nr);
		if (actual_nr)
			return actual_nr;
		return 1;
	}

	/*
	 * If badblocks are present but not in the range, limit known good range
	 * to the requested range.
	 */
	if (bb->count)
		return nr_pages;
#endif
	return PHYS_PFN(tagfs_dev->size - offset);
}

static long tagfs_dax_direct_access(struct dax_device *dax_dev,
		pgoff_t pgoff, long nr_pages, enum dax_access_mode mode,
		void **kaddr, pfn_t *pfn)
{
	struct tagfs_device *tfs = dax_get_private(dax_dev);

	return __tagfs_direct_access(tfs, pgoff, nr_pages, mode, kaddr, pfn);
}


static int
tagfs_dax_zero_page_range(
	struct dax_device *dax_dev,
	pgoff_t            pgoff,
	size_t             nr_pages)
{
	struct tagfs_device *tfs = dax_get_private(dax_dev);

	return blk_status_to_errno(pmem_do_write(tfs, ZERO_PAGE(0), 0,
				   PFN_PHYS(pgoff) >> SECTOR_SHIFT,
				   PAGE_SIZE));
}

/*
 * The recovery write thread started out as a normal pwrite thread and
 * when the filesystem was told about potential media error in the
 * range, filesystem turns the normal pwrite to a dax_recovery_write.
 *
 * The recovery write consists of clearing media poison, clearing page
 * HWPoison bit, reenable page-wide read-write permission, flush the
 * caches and finally write.  A competing pread thread will be held
 * off during the recovery process since data read back might not be
 * valid, and this is achieved by clearing the badblock records after
 * the recovery write is complete. Competing recovery write threads
 * are already serialized by writer lock held by dax_iomap_rw().
 */
static size_t
tagfs_recovery_write(
	struct dax_device *dax_dev,
	pgoff_t            pgoff,
	void              *addr,
	size_t             bytes,
	struct iov_iter   *i)
{
	struct tagfs_device *tfs = dax_get_private(dax_dev);
	size_t olen, len, off;
	phys_addr_t pmem_off;
	struct device *dev = tfs->bb.dev;
	long cleared;

	off = offset_in_page(addr);
	len = PFN_PHYS(PFN_UP(off + bytes));
	if (!is_bad_pmem(&tfs->bb, PFN_PHYS(pgoff) >> SECTOR_SHIFT, len))
		return _copy_from_iter_flushcache(addr, bytes, i);

	/*
	 * Not page-aligned range cannot be recovered. This should not
	 * happen unless something else went wrong.
	 */
	if (off || !PAGE_ALIGNED(bytes)) {
		dev_dbg(dev, "Found poison, but addr(%p) or bytes(%#zx) not page aligned\n",
			addr, bytes);
		return 0;
	}

	pmem_off = PFN_PHYS(pgoff) + tfs->data_offset;
	cleared = __pmem_clear_poison(tfs, pmem_off, len);
	if (cleared > 0 && cleared < len) {
		dev_dbg(dev, "poison cleared only %ld out of %zu bytes\n",
			cleared, len);
		return 0;
	}
	if (cleared < 0) {
		dev_dbg(dev, "poison clear failed: %ld\n", cleared);
		return 0;
	}

	olen = _copy_from_iter_flushcache(addr, bytes, i);
	pmem_clear_bb(tfs, to_sect(tfs, pmem_off), cleared >> SECTOR_SHIFT);

	return olen;
}

static const struct dax_operations tagfs_dax_ops = {
	.direct_access =   tagfs_dax_direct_access,
	.zero_page_range = tagfs_dax_zero_page_range,
	.recovery_write =  tagfs_recovery_write,
};

#else
static const struct dax_operations tagfs_dax_ops = {
	.direct_access =   dax_direct_access,
	.zero_page_range = dax_zero_page_range,
	.recovery_write =  dax_recovery_write,
};
#endif

/**************************************************************************************/

//struct dev_dax;
int add_dax_ops(struct dax_device *dax_dev,
		const struct dax_operations *ops);

static int
tagfs_open_char_device(
	struct super_block *sb,
	struct fs_context  *fc)
{
	struct tagfs_fs_info *fsi = sb->s_fs_info;
	struct dax_device    *dax_devp;
	struct inode         *daxdev_inode;
	//struct dev_dax *dev_dax = filp->private_data;

	int rc = 0;

	printk(KERN_ERR "%s: Not a block device; trying character dax\n", __func__);
	fsi->dax_filp = filp_open(fc->source, O_RDWR, 0);
	printk(KERN_INFO "%s: dax_filp=%llx\n", __func__, (u64)fsi->dax_filp);
        if (IS_ERR(fsi->dax_filp)) {
		printk(KERN_ERR "%s: failed to open dax device\n", __func__);
		fsi->dax_filp = NULL;
		return PTR_ERR(fsi->dax_filp);
	}

	daxdev_inode = file_inode(fsi->dax_filp);
	dax_devp     = inode_dax(daxdev_inode);
	if (IS_ERR(dax_devp)) {
		printk(KERN_ERR "%s: unable to get daxdev from inode\n",
		       __func__);
		rc = -ENODEV;
		goto char_err;
	}
	//dev_dax = dax_get_private(dax_dev);
	printk(KERN_INFO "%s: root dev is character dax (%s) dax_devp (%llx)\n",
	       __func__, fc->source, (u64)dax_devp);

#if 0
	tagfs_dev = kzalloc(sizeof(*tagfs_dev));
	if (IS_ERR(tagfs_dev)) {
		rc = -ENOMEM;
		goto char_err;
	}

	/* XXX need to get the phys_addr and some other stuff to store in struct tagfs_device
	 */
	/* Get phys_addr from dax somehow */
	tagfs_dev->phys_addr = dax_pgoff_to_phys(dev_dax, 0, PAGE_SIZE);
	tagfs_dev->pfn_flags = PFN_DEV;
	
	addr = devm_memremap(dev, pmem->phys_addr,
				pmem->size, ARCH_MEMREMAP_PMEM);
		bb_range.start =  res->start;
		bb_range.end = res->end;
		printk(KERN_NOTICE "%s: is_nd_pfn addr %llx\n",
		       __func__, (u64)addr);
#endif
	/* JG: I aded this function to drivers/dax/super.c */
	rc = add_dax_ops(dax_devp, &tagfs_dax_ops);
	if (rc) {
		printk(KERN_INFO "%s: err attaching tagfs_dax_ops\n", __func__);
		goto char_err;
	}

	fsi->bdevp    = NULL;
	fsi->dax_devp = dax_devp;

	return 0;

char_err:
	filp_close(fsi->dax_filp, NULL);
	return rc;
}

static int
tagfs_open_device(
	struct super_block *sb,
	struct fs_context  *fc)
{
	struct tagfs_fs_info *fsi = sb->s_fs_info;
	struct block_device  *bdevp;
	struct dax_device    *dax_devp;

	u64 start_off = 0;

	if (fsi->dax_devp) {
		printk(KERN_ERR "%s: already mounted\n", __func__);
		return -EALREADY;
	}
	printk("%s: Root device is %s\n", __func__, fc->source);

	if (strstr(fc->source, "/dev/dax"))
		return tagfs_open_char_device(sb, fc);
		
	/* Is this a block device? Find out by trying */
	bdevp = blkdev_get_by_path(fc->source, tagfs_blkdev_mode, fsi);
	if (!bdevp || !IS_ERR(bdevp))
		return tagfs_open_char_device(sb, fc);

	dax_devp = fs_dax_get_by_bdev(bdevp, &start_off,
				      fsi  /* holder */,
				      &tagfs_dax_holder_operations);
	if (IS_ERR(dax_devp)) {
		printk(KERN_ERR "%s: unable to get daxdev from bdevp\n",
		       __func__);
		blkdev_put(bdevp, tagfs_blkdev_mode);
		return -ENODEV;
	}
	printk(KERN_INFO "%s: dax_devp %llx\n", __func__, (u64)dax_devp);
	fsi->bdevp    = bdevp;
	fsi->dax_devp = dax_devp;

	printk(KERN_NOTICE "%s: root device is block dax (%s)\n", __func__, fc->source);
	return 0;
}

static int
tagfs_fill_super(
	struct super_block *sb,
	struct fs_context  *fc)
{
	struct tagfs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode;
	int rc = 0;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= TAGFS_MAGIC;
	sb->s_op		= &tagfs_ops;
	sb->s_time_gran		= 1;

	rc = tagfs_open_device(sb, fc);
	if (rc)
		goto out;

	inode = tagfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		rc = -ENOMEM;

out:
	return rc;
}

static int tagfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, tagfs_fill_super);
}

static void tagfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations tagfs_context_ops = {
	.free		= tagfs_free_fc,
	.parse_param	= tagfs_parse_param,
	.get_tree	= tagfs_get_tree,
};

int tagfs_init_fs_context(struct fs_context *fc)
{
	struct tagfs_fs_info *fsi;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	mutex_init(&fsi->fsi_mutex);
	fsi->mount_opts.mode = TAGFS_DEFAULT_MODE;
	fc->s_fs_info        = fsi;
	fc->ops              = &tagfs_context_ops;
	return 0;
}

static void tagfs_kill_sb(struct super_block *sb)
{
	struct tagfs_fs_info *fsi = sb->s_fs_info;

	mutex_destroy(&fsi->fsi_mutex);
	if (fsi->bdevp)
		blkdev_put(fsi->bdevp, tagfs_blkdev_mode);
	if (fsi->dax_filp)
		filp_close(fsi->dax_filp, NULL);
	if (fsi->dax_devp)
		fs_put_dax(fsi->dax_devp, fsi);

	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct file_system_type tagfs_fs_type = {
	.name		  = "tagfs",
	.init_fs_context  = tagfs_init_fs_context,
	.parameters	  = tagfs_fs_parameters,
	.kill_sb	  = tagfs_kill_sb,
	.fs_flags	  = FS_USERNS_MOUNT,
};

static int __init init_tagfs_fs(void)
{
	printk("%s\n", __func__);
	/* See what the different log levels do */
	printk(KERN_DEBUG    "%s: KERN_DEBUG \n", __func__);
	printk(KERN_INFO     "%s: KERN_INFO \n", __func__);
	printk(KERN_NOTICE   "%s: KERN_NOTICE \n", __func__);
	printk(KERN_WARNING  "%s: KERN_WARNING \n", __func__);
	printk(KERN_ERR      "%s: KERN_ERR \n", __func__);

	return register_filesystem(&tagfs_fs_type);
}

void
__exit tagfs_exit(void)
{
	printk("%s\n", __func__);
	unregister_filesystem(&tagfs_fs_type);
	printk("%s: unregistered\n", __func__);
}

MODULE_AUTHOR("John Groves, Micron Technology");
MODULE_LICENSE("GPL v2");

fs_initcall(init_tagfs_fs);
module_exit(tagfs_exit);
