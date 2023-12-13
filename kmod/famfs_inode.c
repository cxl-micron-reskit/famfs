/* SPDX-License-Identifier: GPL-2.0 */
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023 Micron Technology, inc
 *
 * This file system, originally based on ramfs the dax support from xfs,
 * is intended to allow multiple host systems to mount a common file system
 * view of dax files that map to shared memory.
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
#include <linux/blkdev.h>

#include "famfs.h"
#include "famfs_internal.h"
#include "famfs_trace.h"

/* Because this is exported but only prototyped in dax-private.h: */
struct dax_device *inode_dax(struct inode *inode);

#define FAMFS_DEFAULT_MODE	0755

static const struct super_operations famfs_ops;
static const struct inode_operations famfs_dir_inode_operations;

struct inode *famfs_get_inode(
	struct super_block *sb,
	const struct inode *dir,
	umode_t             mode,
	dev_t               dev)
{
	struct inode *inode = new_inode(sb);

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
			inode->i_op = &famfs_file_inode_operations;
			inode->i_fop = &famfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &famfs_dir_inode_operations;
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
famfs_mknod(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	umode_t           mode,
	dev_t             dev)
{
	struct inode *inode = famfs_get_inode(dir->i_sb, dir, mode, dev);
	int error           = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

static int famfs_mkdir(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	umode_t           mode)
{
	int retval = famfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);

	if (!retval)
		inc_nlink(dir);

	return retval;
}

static int famfs_create(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	umode_t           mode,
	bool              excl)
{
	return famfs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int famfs_symlink(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct dentry    *dentry,
	const char       *symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = famfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
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

static int famfs_tmpfile(
	struct mnt_idmap *idmap,
	struct inode     *dir,
	struct file      *file,
	umode_t           mode)
{
	struct inode *inode;

	inode = famfs_get_inode(dir->i_sb, dir, mode, 0);
	if (!inode)
		return -ENOSPC;

	d_tmpfile(file, inode);
	return finish_open_simple(file, 0);
}

static const struct inode_operations famfs_dir_inode_operations = {
	.create		= famfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= famfs_symlink,
	.mkdir		= famfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= famfs_mknod,
	.rename		= simple_rename,
	.tmpfile	= famfs_tmpfile,
};

/*
 * Display the mount options in /proc/mounts.
 */
static int famfs_show_options(
	struct seq_file *m,
	struct dentry   *root)
{
	struct famfs_fs_info *fsi = root->d_sb->s_fs_info;

	if (fsi->mount_opts.mode != FAMFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mount_opts.mode);

	return 0;
}

static const struct super_operations famfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= famfs_show_options,
};

enum famfs_param {
	Opt_mode,
	Opt_dax,
	Opt_rootdev,
	Opt_daxdev,
};

const struct fs_parameter_spec famfs_fs_parameters[] = {
	fsparam_u32oct("mode",	  Opt_mode),
	fsparam_string("dax",     Opt_dax),
	fsparam_string("rootdev", Opt_rootdev),
	fsparam_string("daxdev",  Opt_daxdev),
	{}
};

static int famfs_parse_param(
	struct fs_context   *fc,
	struct fs_parameter *param)
{
	struct famfs_fs_info *fsi = fc->s_fs_info;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, famfs_fs_parameters, param, &result);
	if (opt == -ENOPARAM) {
		opt = vfs_parse_fs_param_source(fc, param);
		if (opt != -ENOPARAM)
			return opt;
		/*
		 * We might like to report bad mount options here;
		 * but traditionally famfs has ignored all mount options,
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
			pr_notice("%s: invalid dax mode %s\n",
				  __func__, param->string);
		break;
	}

	return 0;
}

/**************************************************************************************/


#if defined(CONFIG_DEV_DAX_IOMAP)

/*
 * For char dax:
 */
static int
famfs_dax_notify_failure(struct dax_device *dax_dev, u64 offset,
			u64 len, int mf_flags)
{
	pr_err("%s: offset %lld len %llu flags %x\n", __func__,
	       offset, len, mf_flags);
	dump_stack();
	return -EOPNOTSUPP;
}

static const struct dax_holder_operations famfs_dax_holder_ops = {
	.notify_failure		= famfs_dax_notify_failure,
};

/**
 * struct @dax_operations
 *
 * /dev/pmem driver has its own dax operation handers, but since any given operation
 * is just a contiguous map-through to a dax device, the "standard" ones in
 * drivers/dax/super.c should be sufficient.
 */
static const struct dax_operations famfs_dax_ops = {
	.direct_access =   dax_direct_access,
	.zero_page_range = dax_zero_page_range,
	.recovery_write =  dax_recovery_write,
};


/**************************************************************************************/

#if 0
int add_dax_ops(struct dax_device *dax_dev,
		const struct dax_operations *ops);
#endif

static int
famfs_open_char_device(
	struct super_block *sb,
	struct fs_context  *fc)
{
	struct famfs_fs_info *fsi = sb->s_fs_info;
	struct dax_device    *dax_devp;
	struct inode         *daxdev_inode;
	//struct dev_dax *dev_dax = filp->private_data;

	int rc = 0;

	pr_err("%s: Not a block device; trying character dax\n", __func__);
	fsi->dax_filp = filp_open(fc->source, O_RDWR, 0);
	pr_info("%s: dax_filp=%llx\n", __func__, (u64)fsi->dax_filp);
	if (IS_ERR(fsi->dax_filp)) {
		pr_err("%s: failed to open dax device %s\n",
		       __func__, fc->source);
		fsi->dax_filp = NULL;
		return PTR_ERR(fsi->dax_filp);
	}

	daxdev_inode = file_inode(fsi->dax_filp);
	dax_devp     = inode_dax(daxdev_inode);
	if (IS_ERR(dax_devp)) {
		pr_err("%s: unable to get daxdev from inode for %s\n",
		       __func__, fc->source);
		rc = -ENODEV;
		goto char_err;
	}
	//dev_dax = dax_get_private(dax_dev);
	pr_info("%s: root dev is character dax (%s) dax_devp (%llx)\n",
	       __func__, fc->source, (u64)dax_devp);

#if 0
	famfs_dev = kzalloc(sizeof(*famfs_dev));
	if (IS_ERR(famfs_dev)) {
		rc = -ENOMEM;
		goto char_err;
	}

	/* XXX need to get the phys_addr and some other stuff to store in struct famfs_device
	 */
	/* Get phys_addr from dax somehow */
	famfs_dev->phys_addr = dax_pgoff_to_phys(dev_dax, 0, PAGE_SIZE);
	famfs_dev->pfn_flags = PFN_DEV;

	xaddr = devm_memremap(dev, pmem->phys_addr,
				pmem->size, ARCH_MEMREMAP_PMEM);
	bb_range.start =  res->start;
	bb_range.end = res->end;
	pr_notice("%s: is_nd_pfn addr %llx\n",
		  __func__, (u64)addr);
#endif
	/*
	 * JG: I aded this function to drivers/dax/super.c
	 * It is compiled if CONFIG_DEV_DAX_IOMAP is defined in the kernel config
	 */
	rc = add_dax_ops(dax_devp, &famfs_dax_holder_ops);
	if (rc) {
		pr_info("%s: err attaching famfs_dax_ops\n", __func__);
		goto char_err;
	}

	fsi->bdevp    = NULL;
	fsi->dax_devp = dax_devp;

	return 0;

char_err:
	filp_close(fsi->dax_filp, NULL);
	return rc;
}

#else /* CONFIG_DEV_DAX_IOMAP */

static int
famfs_open_char_device(
	struct super_block *sb,
	struct fs_context  *fc)
{
	pr_info("%s: Root device is %s, but /dev/dax support compiled out\n",
		__func__, fc->source);
	return -ENODEV;
}

#endif /* CONFIG_DEV_DAX_IOMAP */

static void
famfs_bdev_mark_dead(
       struct block_device     *bdev)
{
	pr_err("%s: Linux thinks something went wrong with the block device!!\n", __func__);
	dump_stack();
	return; /* moving off blkdev anyway; some similar path will need to exist */
}

static const struct blk_holder_ops famfs_blk_holder_ops = {
	.mark_dead  = famfs_bdev_mark_dead,
};

/*
 * For block dax
 */
static int
famfs_blk_dax_notify_failure(
	struct dax_device	*dax_devp,
	u64			offset,
	u64			len,
	int			mf_flags)
{

	pr_err("%s: dax_devp %llx offset %llx len %lld mf_flags %x\n",
	       __func__, (u64)dax_devp, (u64)offset, (u64)len, mf_flags);
	dump_stack();
	return -EOPNOTSUPP;
}

const struct dax_holder_operations famfs_blk_dax_holder_ops = {
	.notify_failure		= famfs_blk_dax_notify_failure,
};


static int
famfs_open_device(
	struct super_block *sb,
	struct fs_context  *fc)
{
	struct famfs_fs_info *fsi = sb->s_fs_info;
	struct block_device  *bdevp;
	struct dax_device    *dax_devp;

	u64 start_off = 0;

	if (fsi->dax_devp) {
		pr_err("%s: already mounted\n", __func__);
		return -EALREADY;
	}

	if (strstr(fc->source, "/dev/dax"))
		return famfs_open_char_device(sb, fc);

	if (!strstr(fc->source, "/dev/pmem")) {
		pr_err("%s: primary backing dev (%s) is not pmem\n",
		       __func__, fc->source);
		return -EINVAL;
	}

	/* Open block/dax backing device */
	bdevp = blkdev_get_by_path(fc->source, famfs_blkdev_mode, fsi,
				   &famfs_blk_holder_ops);
	if (IS_ERR(bdevp)) {
		pr_err("%s: failed blkdev_get_by_path(%s)\n", __func__, fc->source);
		return PTR_ERR(bdevp);
	}

	dax_devp = fs_dax_get_by_bdev(bdevp, &start_off,
				      fsi  /* holder */,
				      &famfs_blk_dax_holder_ops);
	if (IS_ERR(dax_devp)) {
		pr_err("%s: unable to get daxdev from bdevp\n", __func__);
		blkdev_put(bdevp, fsi);
		return -ENODEV;
	}

	fsi->bdevp    = bdevp;
	fsi->dax_devp = dax_devp;

	pr_notice("%s: root device is block dax (%s)\n", __func__, fc->source);
	return 0;
}

static int
famfs_fill_super(
	struct super_block *sb,
	struct fs_context  *fc)
{
	struct famfs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode;
	int rc = 0;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= FAMFS_MAGIC;
	sb->s_op		= &famfs_ops;
	sb->s_time_gran		= 1;

	rc = famfs_open_device(sb, fc);
	if (rc)
		goto out;

	inode = famfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		rc = -ENOMEM;

out:
	return rc;
}

static int famfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, famfs_fill_super);
}

static void famfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations famfs_context_ops = {
	.free		= famfs_free_fc,
	.parse_param	= famfs_parse_param,
	.get_tree	= famfs_get_tree,
};

int famfs_init_fs_context(struct fs_context *fc)
{
	struct famfs_fs_info *fsi;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	mutex_init(&fsi->fsi_mutex);
	fsi->mount_opts.mode = FAMFS_DEFAULT_MODE;
	fc->s_fs_info        = fsi;
	fc->ops              = &famfs_context_ops;
	return 0;
}

static void famfs_kill_sb(struct super_block *sb)
{
	struct famfs_fs_info *fsi = sb->s_fs_info;

	mutex_destroy(&fsi->fsi_mutex);
	if (fsi->bdevp)
		blkdev_put(fsi->bdevp, fsi);
	if (fsi->dax_filp)
		filp_close(fsi->dax_filp, NULL);
	if (fsi->dax_devp)
		fs_put_dax(fsi->dax_devp, fsi);

	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

#define MODULE_NAME "famfs"
static struct file_system_type famfs_fs_type = {
	.name		  = MODULE_NAME,
	.init_fs_context  = famfs_init_fs_context,
	.parameters	  = famfs_fs_parameters,
	.kill_sb	  = famfs_kill_sb,
	.fs_flags	  = FS_USERNS_MOUNT,
};

extern struct attribute_group famfs_attr_group;
static struct kobject *famfs_kobj;

static int __init init_famfs_fs(void)
{
	int rc;

	pr_info("%s\n", __func__);
	/* See what the different log levels do */
	pr_debug("%s: KERN_DEBUG \n", __func__);
	pr_info("%s: KERN_INFO \n", __func__);
	pr_notice("%s: KERN_NOTICE \n", __func__);
	pr_warn("%s: KERN_WARNING \n", __func__);
	pr_err("%s: KERN_ERR \n", __func__);

#if defined(CONFIG_DEV_DAX_IOMAP)
	pr_notice("%s: famfs compiled with experimental /dev/dax support\n", __func__);
#else
	pr_notice("%s: famfs compiled WITHOUT experimental /dev/dax support\n", __func__);
#endif
	famfs_kobj = kobject_create_and_add(MODULE_NAME, fs_kobj);
	if (!famfs_kobj) {
		printk(KERN_ALERT "Failed to create kobject\n");
		return -ENOMEM;
	}

	rc = sysfs_create_group(famfs_kobj, &famfs_attr_group);
	if (rc) {
		kobject_put(famfs_kobj);
		printk(KERN_ALERT "%s: Failed to create sysfs group\n", __func__);
		return rc;
	}

	return register_filesystem(&famfs_fs_type);
}

void
__exit famfs_exit(void)
{
	pr_info("%s\n", __func__);
	sysfs_remove_group(famfs_kobj,  &famfs_attr_group);
	kobject_put(famfs_kobj);
	unregister_filesystem(&famfs_fs_type);
	pr_info("%s: unregistered\n", __func__);
}

MODULE_AUTHOR("John Groves, Micron Technology");
MODULE_LICENSE("GPL v2");

fs_initcall(init_famfs_fs);
module_exit(famfs_exit);
