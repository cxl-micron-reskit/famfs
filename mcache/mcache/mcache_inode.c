/*
 * Copyright (C) 2015-2016 Micron Technology, Inc.  All rights reserved.
 *
 * mcache subsystem for caching mblocks from the mpool subsystem
 * based on ramfs from Linux
 */
/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/cdev.h>

#include <mse_platform/platform.h>
#include <mse_platform/logging.h>

#include <mse_impctl/impool.h>
#include <mpcore/mlog.h>
#include <mpool/init.h>

#include <mcache_internal.h>
#include <mcache_ioctl.h>

#include <mpctl_k_internal.h>

#define MCACHE_READPAGE_DELAY       1000    /* milliseconds */
#define MCACHE_READPAGE_RETRIES     9       /* max retries */
#define MCACHE_DEFAULT_MODE         0755

static const struct super_operations mcache_ops;
static const struct inode_operations mcache_dir_inode_operations;

enum {
	OPT_MODE,
	OPT_FORCE,
	OPT_ERR
};

static const match_table_t tokens = {
	{ OPT_MODE, "mode=%o" },
	{ OPT_FORCE, "force" },
	{ OPT_ERR, NULL }
};

/* Arguments required to initiate an asynchronous call to mblock_read()
 * and which must also be preserved across that call.
 */
struct readpage_args {
	struct mblock_descriptor   *a_mbdesc;
	struct mpool_descriptor    *a_mpdesc;
	struct page                *a_page;
	u64                         a_mboffset;
	int                         a_retries;
};

struct readpage_work {
	struct delayed_work     w_work;
	struct readpage_args    w_args;
};

/**
 * mcache_readpage_cb() - mcache_readpage() callback
 * @work:   w_work.work from struct readpage_work
 *
 * This function is a workqueue callback for the purpose of ensuring
 * that we have process context before calling mblock_read().  To be
 * clear, it is usually called directly by mcache_readpage(), but it
 * may be called indirectly via a workqueue thread if mcache_read()
 * was called from interrupt context and/or to retry errors.
 */
static
void
mcache_readpage_cb(
	struct work_struct *work)
{
	struct readpage_work       *w;
	struct readpage_args        a;
	struct iovec                iov;
	merr_t                      err;

	/* Preserve the incoming arguments.
	 */
	w = container_of(work, struct readpage_work, w_work.work);
	a = w->w_args;

	w = NULL; /* Do not touch! */

	iov.iov_base = page_address(a.a_page);
	iov.iov_len = PAGE_SIZE;

	err = mblock_read(a.a_mpdesc, a.a_mbdesc, &iov, 1, a.a_mboffset);
	if (!err) {
		SetPageUptodate(a.a_page);
		unlock_page(a.a_page);
		return;
	}

	/* Retry all errors a few times so as to avoid transient errors
	 * eliciting the mm to send a SIGBUS to the faulting application.
	 *
	 * TODO: Once we better understand the range of errors we
	 * might get back we can be more selective...
	 */
	if (--a.a_retries < 0) {
		unlock_page(a.a_page);
		ec_count();
		return;
	}

	w = container_of(work, struct readpage_work, w_work.work);

	INIT_DELAYED_WORK(&w->w_work, mcache_readpage_cb);
	w->w_args = a;

	mpool_queue_delayed_work(w->w_args.a_mpdesc, &w->w_work,
				 (MCACHE_READPAGE_DELAY * HZ) / 1000);
	ec_count();
}

/**
 * mcache_readpage() - Fill a single mcache map file page from an mblock
 * @file:
 * @page:
 *
 * This function is called by the mm subsystem (e.g., filemap_fault) when
 * a request is made for a page in an mcache map file that is not resident
 * in the page cache.
 *
 * It uses the metadata stored when the mcache map was created to find the
 * appropriate page from the appropriate mblock in the appropriate mpool.
 */
static
int
mcache_readpage(
	struct file    *file,
	struct page    *page)
{
	struct mcache_map_meta *map;
	struct mcache_fs_info  *fsi;
	struct readpage_work   *w;
	struct inode           *inode;
	struct iovec            iov;

	off_t   offset_in_file;
	loff_t  i_size;
	uint    mbnum;

	inode = file->f_inode;

	i_size = i_size_read(inode);

	if (page->index >= (i_size  >> PAGE_SHIFT))
		return ec_count(-EINVAL);

	fsi = inode->i_sb->s_fs_info;
	map = inode->i_private;

	/* mcm_bktsz is at least as large as the largest mblock, which
	 * means the calculated offset into an mblock could actually
	 * extend beyond an mblock's valid data.  If the app makes such
	 * a request, it'll get a SIGBUS after mblock_read() fails.
	 */
	offset_in_file = page->index << PAGE_SHIFT;
	mbnum = offset_in_file / map->mcm_bktsz;

	if (mbnum >= map->mcm_mbdescc)
		return ec_count(-EINVAL);

	/* Store our readpage_work directly into the page to avoid
	 * the mess of allocating it separately.
	 */
	w = page_address(page);
	w->w_args.a_mbdesc = map->mcm_mbdescv[mbnum];
	w->w_args.a_mpdesc = fsi->fsi_mpdesc;
	w->w_args.a_page = page;
	w->w_args.a_mboffset = offset_in_file % map->mcm_bktsz;
	w->w_args.a_retries = MCACHE_READPAGE_RETRIES;

	iov.iov_base = page_address(page);
	iov.iov_len = PAGE_SIZE;

	if (in_interrupt()) {
		INIT_WORK(&w->w_work.work, mcache_readpage_cb);
		mpool_queue_work(w->w_args.a_mpdesc, &w->w_work.work);
	} else {
		mcache_readpage_cb(&w->w_work.work);
	}

	return 0;
}

static const struct address_space_operations mcache_aops = {
	.readpage       = mcache_readpage,
};

/**
 * mcache_get_inode() - Create an inode
 * @sb:
 * @dir:
 * @mode:
 * @dev:
 *
 * Create an inode with the given parameters.
 */
static
struct inode *
mcache_get_inode(
	struct super_block *sb,
	const struct inode *dir,
	umode_t             mode,
	dev_t               dev)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
		return ec_count(NULL);

	inode->i_ino = get_next_ino();
	inode_init_owner(inode, dir, mode);
	inode->i_mapping->a_ops = &mcache_aops;
	mapping_set_gfp_mask(inode->i_mapping, GFP_USER);
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	switch (mode & S_IFMT) {
	default:
		init_special_inode(inode, mode, dev);
		break;

	case S_IFREG:
		inode->i_op = &mcache_file_inode_operations;
		inode->i_fop = &mcache_file_operations;
		break;

	case S_IFDIR:
		inode->i_op = &mcache_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* directory inodes start off with i_nlink == 2
		 * (for "." entry) */
		inc_nlink(inode);
		break;

	case S_IFLNK:
		inode->i_op = &page_symlink_inode_operations;
		break;
	}

	return inode;
}

/**
 * mcache_mknod() - Allocate an inode
 * @sb:
 * @dir:
 * @mode:
 * @dev:
 *
 * File creation. Allocate an inode, and we're done..
 *
 * SMP-safe
 */
static int
mcache_mknod(
	struct inode   *dir,
	struct dentry  *dentry,
	umode_t         mode,
	dev_t           dev)
{
	struct inode *inode;

	inode = mcache_get_inode(dir->i_sb, dir, mode, dev);
	if (!inode)
		return ec_count(-ENOSPC);

	d_instantiate(dentry, inode);
	dget(dentry);	/* Extra count - pin the dentry in core */
	dir->i_mtime = dir->i_ctime = CURRENT_TIME;

	return 0;
}

/**
 * mcache_mkdir() - Create a directory
 * @dir:
 * @dentry:
 * @mode:
 */
static
int
mcache_mkdir(
	struct inode   *dir,
	struct dentry  *dentry,
	umode_t         mode)
{
	int rc;

	rc = mcache_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (rc)
		return ec_count(rc);

	inc_nlink(dir);

	return 0;
}

/**
 * mcache_create() - Create a regular file
 * @dir:
 * @dentry:
 * @mode:
 * @excl:
 */
static int
mcache_create(
	struct inode   *dir,
	struct dentry  *dentry,
	umode_t         mode,
	bool            excl)
{
	int rc;

	rc = mcache_mknod(dir, dentry, mode | S_IFREG, 0);
	if (rc)
		return ec_count(rc);

	return 0;
}

/**
 * mcache_symlink() - Create a symlink
 * @dir:
 * @dentry:
 * @symname:
 */
static
int
mcache_symlink(
	struct inode   *dir,
	struct dentry  *dentry,
	const char     *symname)
{
	struct inode   *inode;
	int             rc;

	inode = mcache_get_inode(dir->i_sb, dir, S_IFLNK | S_IRWXUGO, 0);
	if (!inode)
		return ec_count(-ENOSPC);

	rc = page_symlink(inode, symname, strlen(symname) + 1);
	if (rc) {
		iput(inode);
		return ec_count(rc);
	}

	d_instantiate(dentry, inode);
	dget(dentry);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME;

	return 0;
}

static const struct inode_operations mcache_dir_inode_operations = {
	.create         = mcache_create,
	.lookup         = simple_lookup,
	.link           = simple_link,
	.unlink         = simple_unlink,
	.symlink        = mcache_symlink,
	.mkdir          = mcache_mkdir,
	.rmdir          = simple_rmdir,
	.mknod          = mcache_mknod,
	.rename         = simple_rename,
};

/**
 * mcache_evict_inode() - Evict this inode
 * @inode:
 *
 * This inode is going away, release all its pages and all our
 * private data.
 */
static void
mcache_evict_inode(
	struct inode   *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	mcache_map_meta_free(inode->i_private);
}

static const struct super_operations mcache_ops = {
	.statfs         = simple_statfs,
	.drop_inode     = generic_delete_inode,
	.show_options   = generic_show_options,
	.evict_inode    = mcache_evict_inode,
};


/**
 * mcache_parse_options() -
 * @data:
 * @opts:
 *
 * mcachefs mount options:
 *
 */
static
int
mcache_parse_options(
	char                       *data,
	struct mcache_mount_opts   *opts)
{
	substring_t args[MAX_OPT_ARGS];
	char       *p;

	opts->mmo_mode = MCACHE_DEFAULT_MODE;

	/* TODO: Probably shouldn't modify data..
	 */
	while ((p = strsep(&data, ",")) != NULL) {
		int token, option;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);

		switch (token) {
		case OPT_MODE:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mmo_mode = option & S_IALLUGO;
			break;

		case OPT_FORCE:
			if (match_int(&args[0], &option))
				return -EINVAL;
			opts->mmo_force = option;
			break;

		}
	}

	return 0;
}

/**
 * mcache_fill_super() - Set up the mcache fs superblock
 * @sb:
 * @data:
 * @silent:
 *
 * This function does the main work of a mount.  We must find our dataset
 * instance (see mpc_unit_lookup_by_path() below).  We will allow the mount
 * to succeed if the force option is specified, but we won't be able to cache
 * anything.  Unless this is useful for testing, we'll take that out...
 */
static
int
mcache_fill_super(
	struct super_block *sb,
	void               *data,
	int                 silent)
{
	struct mcache_fs_info  *fsi;
	struct inode           *inode;

	char  **argv = data;
	merr_t  err;
	int     rc;

	save_mount_options(sb, argv[1]);

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return ec_count(-ENOMEM);

	sb->s_fs_info = fsi;

	rc = mcache_parse_options(argv[1], &fsi->fsi_mntopts);
	if (rc)
		return ec_count(rc);

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= MCACHE_SUPER_MAGIC;
	sb->s_op		= &mcache_ops;
	sb->s_time_gran		= 1;

	/* Look up the dataset to get its mpool descriptor and dataset ID.
	 * If successful, a reference on the unit is acquired which must be
	 * released by calling mpc_unit_put() when mpdesc and dsid are no
	 * longer needed.
	 */
	err = mpc_unit_lookup_by_path(argv[0], &fsi->fsi_mpdesc,
				      &fsi->fsi_dsid, &fsi->fsi_unit);

	if (err || !fsi->fsi_mpdesc) {
		ec_count();

		if (!fsi->fsi_mntopts.mmo_force)
			return ec_count(-EINVAL);
	}

	/* Create the "mount point" directory.
	 */
	inode = mcache_get_inode(sb, NULL,
				 S_IFDIR | fsi->fsi_mntopts.mmo_mode, 0);
	if (!inode)
		return ec_count(-ENOMEM);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return ec_count(-ENOMEM);

	return 0;
}

/**
 * mcache_mount() - Mount mcache on a dataset
 * @fs_type:
 * @flags:
 * @dev_name:       dataset specifier
 * @data:           mount options
 *
 * mount -t mcache <special> <node>
 *
 * For example:
 *
 *   mount -t mcache /dev/mpool/mp1/ds1 /dev/mpool/mp1/ds1.mcache
 *
 * If we do not find the specified mpool dataset instance the mount
 * will fail unless you add "force" as a mount option.  Then you can
 * have mcache map files, but they have no backing store.  There
 * might be a testing use for this...
 */
static
struct dentry *
mcache_mount(
	struct file_system_type    *fs_type,
	int                         flags,
	const char                 *dev_name,
	void                       *data)
{
	const char     *argv[] = { dev_name, data, NULL };
	struct dentry  *dentry;

	if (!try_module_get(THIS_MODULE))
		return ec_count(NULL);

	dentry = mount_nodev(fs_type, flags, argv, mcache_fill_super);

	if (IS_ERR_OR_NULL(dentry)) {
		module_put(THIS_MODULE);
		ec_count();
	}

	return dentry;
}

/**
 * mcache_kill_sb() - Unmount an mcache mount
 * @sb:
 */
static
void
mcache_kill_sb(
	struct super_block *sb)
{
	struct mcache_fs_info *fsi = sb->s_fs_info;

	/* Release ref on the dataset unit when we dismount.
	 */
	if (fsi) {
		mpc_unit_put(fsi->fsi_unit);

		sb->s_fs_info = NULL;
		kfree(fsi);

		/* If mcache_fill_super() completed successfully then we
		 * must release our hold on the module here.  Otherwise,
		 * it will be released by mcache_mount().
		 */
		if (sb->s_root)
			module_put(THIS_MODULE);
	}

	kill_litter_super(sb);
}

static struct file_system_type mcache_fs_type = {
	.name		= "mcache",
	.mount		= mcache_mount,
	.kill_sb	= mcache_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

int
mcache_init(void)
{
	static unsigned long once;
	int                  rc;

	if (test_and_set_bit(0, &once))
		return 0;

	rc = register_filesystem(&mcache_fs_type);
	if (rc)
		return ec_count(rc);

	return 0;
}

void
mcache_exit(void)
{
	unregister_filesystem(&mcache_fs_type);
}
