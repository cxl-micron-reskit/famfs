/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/
/*
 * Copyright (C) 2024 Micron Technology, Inc.  All rights reserved.
 */

#define _GNU_SOURCE
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/xattr.h>
#include <systemd/sd-journal.h>

#include "../fuse/passthrough_helpers.h"
#include "famfs_lib.h"

/* We are re-using pointers to our `struct famfs_inode` and `struct
   famfs_dirp` elements as inodes. This means that we must be able to
   store uintptr_t values in a fuse_ino_t variable. The following
   incantation checks this condition at compile time. */
#if defined(__GNUC__) && (__GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ >= 6) && !defined __cplusplus
_Static_assert(sizeof(fuse_ino_t) >= sizeof(uintptr_t),
	       "fuse_ino_t too small to hold uintptr_t values!");
#else
struct _uintptr_to_must_hold_fuse_ino_t_dummy_struct \
	{ unsigned _uintptr_to_must_hold_fuse_ino_t:
			((sizeof(fuse_ino_t) >= sizeof(uintptr_t)) ? 1 : -1); };
#endif

struct famfs_inode {
	struct famfs_inode *next; /* protected by lo->mutex */
	struct famfs_inode *prev; /* protected by lo->mutex */
	int fd;
	ino_t ino;
	dev_t dev;
	uint64_t refcount; /* protected by lo->mutex */
};

enum {
	CACHE_NEVER,
	CACHE_NORMAL,
	CACHE_ALWAYS,
};

struct famfs_data {
	pthread_mutex_t mutex;
	int debug;
	int writeback;
	int flock;
	int xattr;
	char *source;
	double timeout;
	int cache;
	int timeout_set;
	struct famfs_inode root; /* protected by ->mutex */
};

static const struct fuse_opt famfs_opts[] = {
	{ "writeback",
	  offsetof(struct famfs_data, writeback), 1 },
	{ "no_writeback",
	  offsetof(struct famfs_data, writeback), 0 },
	{ "source=%s",
	  offsetof(struct famfs_data, source), 0 },
	{ "flock",
	  offsetof(struct famfs_data, flock), 1 },
	{ "no_flock",
	  offsetof(struct famfs_data, flock), 0 },
	{ "xattr",
	  offsetof(struct famfs_data, xattr), 1 },
	{ "no_xattr",
	  offsetof(struct famfs_data, xattr), 0 },
	{ "timeout=%lf",
	  offsetof(struct famfs_data, timeout), 0 },
	{ "timeout=",
	  offsetof(struct famfs_data, timeout_set), 1 },
	{ "cache=never",
	  offsetof(struct famfs_data, cache), CACHE_NEVER },
	{ "cache=auto",
	  offsetof(struct famfs_data, cache), CACHE_NORMAL },
	{ "cache=always",
	  offsetof(struct famfs_data, cache), CACHE_ALWAYS },

	FUSE_OPT_END
};

void dump_fuse_args(struct fuse_args *args)
{
	int i;

	printf("%s: %s\n", __func__, (args->allocated) ? "(allocated)": "");
	for (i = 0; i<args->argc; i++)
		printf("\t%d: %s\n", i, args->argv[i]);

}

static void passthrough_ll_help(void)
{
	printf(
"    -o writeback           Enable writeback\n"
"    -o no_writeback        Disable write back\n"
"    -o source=/home/dir    Source directory to be mounted\n"
"    -o flock               Enable flock\n"
"    -o no_flock            Disable flock\n"
"    -o xattr               Enable xattr\n"
"    -o no_xattr            Disable xattr\n"
"    -o timeout=1.0         Caching timeout\n"
"    -o timeout=0/1         Timeout is set\n"
"    -o cache=never         Disable cache\n"
"    -o cache=auto          Auto enable cache\n"
"    -o cache=always        Cache always\n");
}

static struct famfs_data *famfs_data(fuse_req_t req)
{
	return (struct famfs_data *) fuse_req_userdata(req);
}

static struct famfs_inode *
famfs_inode(
	fuse_req_t req,
	fuse_ino_t ino)
{
	if (ino == FUSE_ROOT_ID)
		return &famfs_data(req)->root;
	else
		return (struct famfs_inode *) (uintptr_t) ino;
}

static int
famfs_fd(fuse_req_t req, fuse_ino_t ino)
{
	return famfs_inode(req, ino)->fd;
}

static bool famfs_debug(fuse_req_t req)
{
	return famfs_data(req)->debug != 0;
}

static void famfs_init(
	void *userdata,
	struct fuse_conn_info *conn)
{
	struct famfs_data *lo = (struct famfs_data*) userdata;

	if (lo->writeback &&
	    conn->capable & FUSE_CAP_WRITEBACK_CACHE) {
		if (lo->debug)
			fuse_log(FUSE_LOG_DEBUG, "famfs_init: activating writeback\n");
		conn->want |= FUSE_CAP_WRITEBACK_CACHE;
	}
	if (lo->flock && conn->capable & FUSE_CAP_FLOCK_LOCKS) {
		if (lo->debug)
			fuse_log(FUSE_LOG_DEBUG, "famfs_init: activating flock locks\n");
		conn->want |= FUSE_CAP_FLOCK_LOCKS;
	}
}

static void famfs_destroy(void *userdata)
{
	struct famfs_data *lo = (struct famfs_data*) userdata;

	while (lo->root.next != &lo->root) {
		struct famfs_inode* next = lo->root.next;
		lo->root.next = next->next;
		close(next->fd);
		free(next);
	}
}

static void
famfs_getattr(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_file_info *fi)
{
	int res;
	struct stat buf;
	struct famfs_data *lo = famfs_data(req);

	(void) fi;

	res = fstatat(famfs_fd(req, ino), "", &buf, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return (void) fuse_reply_err(req, errno);

	fuse_reply_attr(req, &buf, lo->timeout);
}

static void
famfs_setattr(
	fuse_req_t req,
	fuse_ino_t ino,
	struct stat *attr,
	int valid,
	struct fuse_file_info *fi)
{
	int saverr;
	char procname[64];
	struct famfs_inode *inode = famfs_inode(req, ino);
	int ifd = inode->fd;
	int res;

	if (valid & FUSE_SET_ATTR_MODE) {
		if (fi) {
			res = fchmod(fi->fh, attr->st_mode);
		} else {
			sprintf(procname, "/proc/self/fd/%i", ifd);
			res = chmod(procname, attr->st_mode);
		}
		if (res == -1)
			goto out_err;
	}
	if (valid & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
		uid_t uid = (valid & FUSE_SET_ATTR_UID) ?
			attr->st_uid : (uid_t) -1;
		gid_t gid = (valid & FUSE_SET_ATTR_GID) ?
			attr->st_gid : (gid_t) -1;

		res = fchownat(ifd, "", uid, gid,
			       AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
		if (res == -1)
			goto out_err;
	}
	if (valid & FUSE_SET_ATTR_SIZE) {
		if (fi) {
			res = ftruncate(fi->fh, attr->st_size);
		} else {
			sprintf(procname, "/proc/self/fd/%i", ifd);
			res = truncate(procname, attr->st_size);
		}
		if (res == -1)
			goto out_err;
	}
	if (valid & (FUSE_SET_ATTR_ATIME | FUSE_SET_ATTR_MTIME)) {
		struct timespec tv[2];

		tv[0].tv_sec = 0;
		tv[1].tv_sec = 0;
		tv[0].tv_nsec = UTIME_OMIT;
		tv[1].tv_nsec = UTIME_OMIT;

		if (valid & FUSE_SET_ATTR_ATIME_NOW)
			tv[0].tv_nsec = UTIME_NOW;
		else if (valid & FUSE_SET_ATTR_ATIME)
			tv[0] = attr->st_atim;

		if (valid & FUSE_SET_ATTR_MTIME_NOW)
			tv[1].tv_nsec = UTIME_NOW;
		else if (valid & FUSE_SET_ATTR_MTIME)
			tv[1] = attr->st_mtim;

		if (fi)
			res = futimens(fi->fh, tv);
		else {
			sprintf(procname, "/proc/self/fd/%i", ifd);
			res = utimensat(AT_FDCWD, procname, tv, 0);
		}
		if (res == -1)
			goto out_err;
	}

	return famfs_getattr(req, ino, fi);

out_err:
	saverr = errno;
	fuse_reply_err(req, saverr);
}

static struct famfs_inode *famfs_find(struct famfs_data *lo, struct stat *st)
{
	struct famfs_inode *p;
	struct famfs_inode *ret = NULL;

	pthread_mutex_lock(&lo->mutex);
	for (p = lo->root.next; p != &lo->root; p = p->next) {
		if (p->ino == st->st_ino && p->dev == st->st_dev) {
			assert(p->refcount > 0);
			ret = p;
			ret->refcount++;
			break;
		}
	}
	pthread_mutex_unlock(&lo->mutex);
	return ret;
}

static int
famfs_do_lookup(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	struct fuse_entry_param *e)
{
	int newfd;
	int res;
	int saverr;
	struct famfs_data *lo = famfs_data(req);
	struct famfs_inode *inode;

	memset(e, 0, sizeof(*e));
	e->attr_timeout = lo->timeout;
	e->entry_timeout = lo->timeout;

	printf("%s: name=%s\n", __func__, name);
	newfd = openat(famfs_fd(req, parent), name, O_PATH | O_NOFOLLOW, O_RDONLY);
	if (newfd == -1)
		goto out_err;

#if 0
	res = fstatat(newfd, "", &e->attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		goto out_err;
#else
	/* Famfs populates the stat struct from the shadow yaml */
	res = famfs_shadow_to_stat(newfd, &e->attr, 1);
	if (res)
		goto out_err;
#endif
	inode = famfs_find(famfs_data(req), &e->attr);
	if (inode) {
		close(newfd);
		newfd = -1;
	} else {
		struct famfs_inode *prev, *next;

		saverr = ENOMEM;
		inode = calloc(1, sizeof(struct famfs_inode));
		if (!inode)
			goto out_err;

		inode->refcount = 1;
		inode->fd = newfd;
		inode->ino = e->attr.st_ino;
		inode->dev = e->attr.st_dev;

		pthread_mutex_lock(&lo->mutex);
		prev = &lo->root;
		next = prev->next;
		next->prev = inode;
		inode->next = next;
		inode->prev = prev;
		prev->next = inode;
		pthread_mutex_unlock(&lo->mutex);
	}
	e->ino = (uintptr_t) inode;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n",
			(unsigned long long) parent, name, (unsigned long long) e->ino);

	return 0;

out_err:
	saverr = errno;
	if (newfd != -1)
		close(newfd);
	return saverr;
}

static void
famfs_lookup(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	struct fuse_entry_param e;
	int err;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "famfs_lookup(parent=%" PRIu64 ", name=%s)\n",
			parent, name);

	err = famfs_do_lookup(req, parent, name, &e);
	if (err)
		fuse_reply_err(req, err);
	else
		fuse_reply_entry(req, &e);
}

static void
famfs_mknod_symlink(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode,
	dev_t rdev,
	const char *link)
{
	int res;
	int saverr;
	struct famfs_inode *dir = famfs_inode(req, parent);
	struct fuse_entry_param e;

	res = mknod_wrapper(dir->fd, name, link, mode, rdev);

	saverr = errno;
	if (res == -1)
		goto out;

	saverr = famfs_do_lookup(req, parent, name, &e);
	if (saverr)
		goto out;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n",
			(unsigned long long) parent, name, (unsigned long long) e.ino);

	fuse_reply_entry(req, &e);
	return;

out:
	fuse_reply_err(req, saverr);
}

static void
famfs_mknod(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode,
	dev_t rdev)
{
	famfs_mknod_symlink(req, parent, name, mode, rdev, NULL);
}

static void
famfs_fuse_mkdir(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode)
{
	famfs_mknod_symlink(req, parent, name, S_IFDIR | mode, 0, NULL);
}

static void
famfs_symlink(
	fuse_req_t req,
	const char *link,
	fuse_ino_t parent,
	const char *name)
{
	famfs_mknod_symlink(req, parent, name, S_IFLNK, 0, link);
}

static void
famfs_link(
	fuse_req_t req,
	fuse_ino_t ino,
	fuse_ino_t parent,
	const char *name)
{
	int res;
	struct famfs_data *lo = famfs_data(req);
	struct famfs_inode *inode = famfs_inode(req, ino);
	struct fuse_entry_param e;
	char procname[64];
	int saverr;

	memset(&e, 0, sizeof(struct fuse_entry_param));
	e.attr_timeout = lo->timeout;
	e.entry_timeout = lo->timeout;

	sprintf(procname, "/proc/self/fd/%i", inode->fd);
	res = linkat(AT_FDCWD, procname, famfs_fd(req, parent), name,
		     AT_SYMLINK_FOLLOW);
	if (res == -1)
		goto out_err;

	res = fstatat(inode->fd, "", &e.attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		goto out_err;

	pthread_mutex_lock(&lo->mutex);
	inode->refcount++;
	pthread_mutex_unlock(&lo->mutex);
	e.ino = (uintptr_t) inode;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n",
			(unsigned long long) parent, name,
			(unsigned long long) e.ino);

	fuse_reply_entry(req, &e);
	return;

out_err:
	saverr = errno;
	fuse_reply_err(req, saverr);
}

static void
famfs_rmdir(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	/* If files become bi-modal, unlink will be allowed on
	 * uncommitted files & dirs */
	if (famfs_debug(req)) {
		fuse_log(FUSE_LOG_DEBUG,
			 "  Rejecting rmdir for forget %lli %s\n",
			(unsigned long long) parent ,
			 (unsigned long long) name);
	}

	fuse_reply_err(req, EINVAL);
}

static void
famfs_rename(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	fuse_ino_t newparent,
	const char *newname,
	unsigned int flags)
{
	int res;

	if (flags) {
		fuse_reply_err(req, EINVAL);
		return;
	}

	res = renameat(famfs_fd(req, parent), name,
			famfs_fd(req, newparent), newname);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_unlink(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	/* If files become bi-modal, unlink will be allowed on
	 * uncommitted files & dirs */
	if (famfs_debug(req)) {
		fuse_log(FUSE_LOG_DEBUG,
			 "  Rejecting rmdir for forget %lli %s\n",
			(unsigned long long) parent ,
			 (unsigned long long) name);
	}

	fuse_reply_err(req, EINVAL);
}

static void
unref_inode(
	struct famfs_data *lo,
	struct famfs_inode *inode,
	uint64_t n)
{
	if (!inode)
		return;

	pthread_mutex_lock(&lo->mutex);
	assert(inode->refcount >= n);
	inode->refcount -= n;
	if (!inode->refcount) {
		struct famfs_inode *prev, *next;

		prev = inode->prev;
		next = inode->next;
		next->prev = prev;
		prev->next = next;

		pthread_mutex_unlock(&lo->mutex);
		close(inode->fd);
		free(inode);

	} else {
		pthread_mutex_unlock(&lo->mutex);
	}
}

static void
famfs_forget_one(
	fuse_req_t req,
	fuse_ino_t ino,
	uint64_t nlookup)
{
	struct famfs_data *lo = famfs_data(req);
	struct famfs_inode *inode = famfs_inode(req, ino);

	if (famfs_debug(req)) {
		fuse_log(FUSE_LOG_DEBUG, "  forget %lli %lli -%lli\n",
			(unsigned long long) ino,
			(unsigned long long) inode->refcount,
			(unsigned long long) nlookup);
	}

	unref_inode(lo, inode, nlookup);
}

static void
famfs_forget(
	fuse_req_t req,
	fuse_ino_t ino,
	uint64_t nlookup)
{
	famfs_forget_one(req, ino, nlookup);
	fuse_reply_none(req);
}

static void
famfs_forget_multi(
	fuse_req_t req,
	size_t count,
	struct fuse_forget_data *forgets)
{
	int i;

	for (i = 0; i < count; i++)
		famfs_forget_one(req, forgets[i].ino, forgets[i].nlookup);
	fuse_reply_none(req);
}

static void
famfs_readlink(
	fuse_req_t req,
	fuse_ino_t ino)
{
	char buf[PATH_MAX + 1];
	int res;

	res = readlinkat(famfs_fd(req, ino), "", buf, sizeof(buf));
	if (res == -1)
		return (void) fuse_reply_err(req, errno);

	if (res == sizeof(buf))
		return (void) fuse_reply_err(req, ENAMETOOLONG);

	buf[res] = '\0';

	fuse_reply_readlink(req, buf);
}

struct famfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static struct famfs_dirp *
famfs_dirp(struct fuse_file_info *fi)
{
	return (struct famfs_dirp *) (uintptr_t) fi->fh;
}

static void
famfs_opendir(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_file_info *fi)
{
	int error = ENOMEM;
	struct famfs_data *lo = famfs_data(req);
	struct famfs_dirp *d;
	int fd;

	d = calloc(1, sizeof(struct famfs_dirp));
	if (d == NULL)
		goto out_err;

	fd = openat(famfs_fd(req, ino), ".", O_RDONLY);
	if (fd == -1)
		goto out_errno;

	d->dp = fdopendir(fd);
	if (d->dp == NULL)
		goto out_errno;

	d->offset = 0;
	d->entry = NULL;

	fi->fh = (uintptr_t) d;
	if (lo->cache == CACHE_ALWAYS)
		fi->cache_readdir = 1;
	fuse_reply_open(req, fi);
	return;

out_errno:
	error = errno;
out_err:
	if (d) {
		if (fd != -1)
			close(fd);
		free(d);
	}
	fuse_reply_err(req, error);
}

static int
is_dot_or_dotdot(const char *name)
{
	return name[0] == '.' && (name[1] == '\0' ||
				  (name[1] == '.' && name[2] == '\0'));
}

static void
famfs_do_readdir(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi,
	int plus)
{
	struct famfs_dirp *d = famfs_dirp(fi);
	char *buf;
	char *p;
	size_t rem = size;
	int err;

	(void) ino;

	buf = calloc(1, size);
	if (!buf) {
		err = ENOMEM;
		goto error;
	}
	p = buf;

	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		size_t entsize;
		off_t nextoff;
		const char *name;

		if (!d->entry) {
			errno = 0;
			d->entry = readdir(d->dp);
			if (!d->entry) {
				if (errno) {  // Error
					err = errno;
					goto error;
				} else {  // End of stream
					break; 
				}
			}
		}
		nextoff = d->entry->d_off;
		name = d->entry->d_name;
		fuse_ino_t entry_ino = 0;
		if (plus) {
			struct fuse_entry_param e;
			if (is_dot_or_dotdot(name)) {
				e = (struct fuse_entry_param) {
					.attr.st_ino = d->entry->d_ino,
					.attr.st_mode = d->entry->d_type << 12,
				};
			} else {
				err = famfs_do_lookup(req, ino, name, &e);
				if (err)
					goto error;
				entry_ino = e.ino;
			}

			entsize = fuse_add_direntry_plus(req, p, rem, name,
							 &e, nextoff);
		} else {
			struct stat st = {
				.st_ino = d->entry->d_ino,
				.st_mode = d->entry->d_type << 12,
			};
			entsize = fuse_add_direntry(req, p, rem, name,
						    &st, nextoff);
		}
		if (entsize > rem) {
			if (entry_ino != 0) 
				famfs_forget_one(req, entry_ino, 1);
			break;
		}
		
		p += entsize;
		rem -= entsize;

		d->entry = NULL;
		d->offset = nextoff;
	}

    err = 0;
error:
    // If there's an error, we can only signal it if we haven't stored
    // any entries yet - otherwise we'd end up with wrong lookup
    // counts for the entries that are already in the buffer. So we
    // return what we've collected until that point.
    if (err && rem == size)
	    fuse_reply_err(req, err);
    else
	    fuse_reply_buf(req, buf, size - rem);
    free(buf);
}

static void
famfs_readdir(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi)
{
	famfs_do_readdir(req, ino, size, offset, fi, 0);
}

static void
famfs_readdirplus(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi)
{
	famfs_do_readdir(req, ino, size, offset, fi, 1);
}

static void
famfs_releasedir(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_file_info *fi)
{
	struct famfs_dirp *d = famfs_dirp(fi);
	(void) ino;
	closedir(d->dp);
	free(d);
	fuse_reply_err(req, 0);
}

static void
famfs_create(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode,
	struct fuse_file_info *fi)
{
	int fd;
	struct famfs_data *lo = famfs_data(req);
	struct fuse_entry_param e;
	int err;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "famfs_create(parent=%" PRIu64 ", name=%s)\n",
			parent, name);

	fd = openat(famfs_fd(req, parent), name,
		    (fi->flags | O_CREAT) & ~O_NOFOLLOW, mode);
	if (fd == -1)
		return (void) fuse_reply_err(req, errno);

	fi->fh = fd;
	if (lo->cache == CACHE_NEVER)
		fi->direct_io = 1;
	else if (lo->cache == CACHE_ALWAYS)
		fi->keep_cache = 1;

	/* parallel_direct_writes feature depends on direct_io features.
	   To make parallel_direct_writes valid, need set fi->direct_io
	   in current function. */
	fi->parallel_direct_writes = 1;

	err = famfs_do_lookup(req, parent, name, &e);
	if (err)
		fuse_reply_err(req, err);
	else
		fuse_reply_create(req, &e, fi);
}

static void
famfs_fsyncdir(
	fuse_req_t req,
	fuse_ino_t ino,
	int datasync,
	struct fuse_file_info *fi)
{
	int res;
	int fd = dirfd(famfs_dirp(fi)->dp);
	(void) ino;
	if (datasync)
		res = fdatasync(fd);
	else
		res = fsync(fd);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_open(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_file_info *fi)
{
	int fd;
	char buf[64];
	struct famfs_data *lo = famfs_data(req);

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "famfs_open(ino=%" PRIu64 ", flags=%d)\n",
			ino, fi->flags);

	/* With writeback cache, kernel may send read requests even
	   when userspace opened write-only */
	if (lo->writeback && (fi->flags & O_ACCMODE) == O_WRONLY) {
		fi->flags &= ~O_ACCMODE;
		fi->flags |= O_RDWR;
	}

	/* With writeback cache, O_APPEND is handled by the kernel.
	   This breaks atomicity (since the file may change in the
	   underlying filesystem, so that the kernel's idea of the
	   end of the file isn't accurate anymore). In this example,
	   we just accept that. A more rigorous filesystem may want
	   to return an error here */
	if (lo->writeback && (fi->flags & O_APPEND))
		fi->flags &= ~O_APPEND;

	sprintf(buf, "/proc/self/fd/%i", famfs_fd(req, ino));
	fd = open(buf, fi->flags & ~O_NOFOLLOW);
	if (fd == -1)
		return (void) fuse_reply_err(req, errno);

	fi->fh = fd;
	if (lo->cache == CACHE_NEVER)
		fi->direct_io = 1;
	else if (lo->cache == CACHE_ALWAYS)
		fi->keep_cache = 1;

        /* Enable direct_io when open has flags O_DIRECT to enjoy the feature
        parallel_direct_writes (i.e., to get a shared lock, not exclusive lock,
	for writes to the same file in the kernel). */
	if (fi->flags & O_DIRECT)
		fi->direct_io = 1;

	/* parallel_direct_writes feature depends on direct_io features.
	   To make parallel_direct_writes valid, need set fi->direct_io
	   in current function. */
	fi->parallel_direct_writes = 1;

	fuse_reply_open(req, fi);
}

static void
famfs_release(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_file_info *fi)
{
	(void) ino;

	close(fi->fh);
	fuse_reply_err(req, 0);
}

static void
famfs_flush(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_file_info *fi)
{
	int res;
	(void) ino;
	res = close(dup(fi->fh));
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_fsync(
	fuse_req_t req,
	fuse_ino_t ino,
	int datasync,
	struct fuse_file_info *fi)
{
	int res;
	(void) ino;
	if (datasync)
		res = fdatasync(fi->fh);
	else
		res = fsync(fi->fh);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_read(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi)
{
	struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "famfs_read(ino=%" PRIu64 ", size=%zd, "
			"off=%lu)\n", ino, size, (unsigned long) offset);

	buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.buf[0].fd = fi->fh;
	buf.buf[0].pos = offset;

	fuse_reply_data(req, &buf, FUSE_BUF_SPLICE_MOVE);
}

static void
famfs_write_buf(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_bufvec *in_buf,
	off_t off,
	struct fuse_file_info *fi)
{
	(void) ino;
	ssize_t res;
	struct fuse_bufvec out_buf = FUSE_BUFVEC_INIT(fuse_buf_size(in_buf));

	out_buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	out_buf.buf[0].fd = fi->fh;
	out_buf.buf[0].pos = off;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "famfs_write(ino=%" PRIu64 ", size=%zd, off=%lu)\n",
			ino, out_buf.buf[0].size, (unsigned long) off);

	res = fuse_buf_copy(&out_buf, in_buf, 0);
	if(res < 0)
		fuse_reply_err(req, -res);
	else
		fuse_reply_write(req, (size_t) res);
}

static void
famfs_statfs(
	fuse_req_t req,
	fuse_ino_t ino)
{
	int res;
	struct statvfs stbuf;

	res = fstatvfs(famfs_fd(req, ino), &stbuf);
	if (res == -1)
		fuse_reply_err(req, errno);
	else
		fuse_reply_statfs(req, &stbuf);
}

static void
famfs_fallocate(
	fuse_req_t req,
	fuse_ino_t ino,
	int mode,
	off_t offset,
	off_t length,
	struct fuse_file_info *fi)
{
	int err = EOPNOTSUPP;
	(void) ino;

#ifdef HAVE_FALLOCATE
	err = fallocate(fi->fh, mode, offset, length);
	if (err < 0)
		err = errno;

#elif defined(HAVE_POSIX_FALLOCATE)
	if (mode) {
		fuse_reply_err(req, EOPNOTSUPP);
		return;
	}

	err = posix_fallocate(fi->fh, offset, length);
#endif

	fuse_reply_err(req, err);
}

static void
famfs_flock(
	fuse_req_t req,
	fuse_ino_t ino,
	struct fuse_file_info *fi,
	int op)
{
	int res;
	(void) ino;

	res = flock(fi->fh, op);

	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_getxattr(
	fuse_req_t req,
	fuse_ino_t ino,
	const char *name,
	size_t size)
{
	char *value = NULL;
	char procname[64];
	struct famfs_inode *inode = famfs_inode(req, ino);
	ssize_t ret;
	int saverr;

	saverr = ENOSYS;
	if (!famfs_data(req)->xattr)
		goto out;

	if (famfs_debug(req)) {
		fuse_log(FUSE_LOG_DEBUG, "famfs_getxattr(ino=%" PRIu64 ", name=%s size=%zd)\n",
			ino, name, size);
	}

	sprintf(procname, "/proc/self/fd/%i", inode->fd);

	if (size) {
		value = malloc(size);
		if (!value)
			goto out_err;

		ret = getxattr(procname, name, value, size);
		if (ret == -1)
			goto out_err;
		saverr = 0;
		if (ret == 0)
			goto out;

		fuse_reply_buf(req, value, ret);
	} else {
		ret = getxattr(procname, name, NULL, 0);
		if (ret == -1)
			goto out_err;

		fuse_reply_xattr(req, ret);
	}
out_free:
	free(value);
	return;

out_err:
	saverr = errno;
out:
	fuse_reply_err(req, saverr);
	goto out_free;
}

static void
famfs_listxattr(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size)
{
	char *value = NULL;
	char procname[64];
	struct famfs_inode *inode = famfs_inode(req, ino);
	ssize_t ret;
	int saverr;

	saverr = ENOSYS;
	if (!famfs_data(req)->xattr)
		goto out;

	if (famfs_debug(req)) {
		fuse_log(FUSE_LOG_DEBUG, "famfs_listxattr(ino=%" PRIu64 ", size=%zd)\n",
			ino, size);
	}

	sprintf(procname, "/proc/self/fd/%i", inode->fd);

	if (size) {
		value = malloc(size);
		if (!value)
			goto out_err;

		ret = listxattr(procname, value, size);
		if (ret == -1)
			goto out_err;
		saverr = 0;
		if (ret == 0)
			goto out;

		fuse_reply_buf(req, value, ret);
	} else {
		ret = listxattr(procname, NULL, 0);
		if (ret == -1)
			goto out_err;

		fuse_reply_xattr(req, ret);
	}
out_free:
	free(value);
	return;

out_err:
	saverr = errno;
out:
	fuse_reply_err(req, saverr);
	goto out_free;
}

static void
famfs_setxattr(
	fuse_req_t req,
	fuse_ino_t ino,
	const char *name,
	const char *value,
	size_t size,
	int flags)
{
	char procname[64];
	struct famfs_inode *inode = famfs_inode(req, ino);
	ssize_t ret;
	int saverr;

	saverr = ENOSYS;
	if (!famfs_data(req)->xattr)
		goto out;

	if (famfs_debug(req)) {
		fuse_log(FUSE_LOG_DEBUG, "famfs_setxattr(ino=%" PRIu64 ", name=%s value=%s size=%zd)\n",
			ino, name, value, size);
	}

	sprintf(procname, "/proc/self/fd/%i", inode->fd);

	ret = setxattr(procname, name, value, size, flags);
	saverr = ret == -1 ? errno : 0;

out:
	fuse_reply_err(req, saverr);
}

static void
famfs_removexattr(
	fuse_req_t req,
	fuse_ino_t ino,
	const char *name)
{
	char procname[64];
	struct famfs_inode *inode = famfs_inode(req, ino);
	ssize_t ret;
	int saverr;

	saverr = ENOSYS;
	if (!famfs_data(req)->xattr)
		goto out;

	if (famfs_debug(req)) {
		fuse_log(FUSE_LOG_DEBUG, "famfs_removexattr(ino=%" PRIu64 ", name=%s)\n",
			ino, name);
	}

	sprintf(procname, "/proc/self/fd/%i", inode->fd);

	ret = removexattr(procname, name);
	saverr = ret == -1 ? errno : 0;

out:
	fuse_reply_err(req, saverr);
}

#ifdef HAVE_COPY_FILE_RANGE
static void
famfs_copy_file_range(
	fuse_req_t req,
	fuse_ino_t ino_in,
	off_t off_in,
	struct fuse_file_info *fi_in,
	fuse_ino_t ino_out,
	off_t off_out,
	struct fuse_file_info *fi_out,
	size_t len,
	int flags)
{
	ssize_t res;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "famfs_copy_file_range(ino=%" PRIu64 "/fd=%lu, "
				"off=%lu, ino=%" PRIu64 "/fd=%lu, "
				"off=%lu, size=%zd, flags=0x%x)\n",
			ino_in, fi_in->fh, off_in, ino_out, fi_out->fh, off_out,
			len, flags);

	res = copy_file_range(fi_in->fh, &off_in, fi_out->fh, &off_out, len,
			      flags);
	if (res < 0)
		fuse_reply_err(req, errno);
	else
		fuse_reply_write(req, res);
}
#endif

static void
famfs_lseek(
	fuse_req_t req,
	fuse_ino_t ino,
	off_t off,
	int whence,
	struct fuse_file_info *fi)
{
	off_t res;

	(void)ino;
	res = lseek(fi->fh, off, whence);
	if (res != -1)
		fuse_reply_lseek(req, res);
	else
		fuse_reply_err(req, errno);
}

static const struct fuse_lowlevel_ops famfs_oper = {
	.init		= famfs_init,
	.destroy	= famfs_destroy,
	.lookup		= famfs_lookup,
	.mkdir		= famfs_fuse_mkdir,
	.mknod		= famfs_mknod,
	.symlink	= famfs_symlink,
	.link		= famfs_link,
	.unlink		= famfs_unlink,
	.rmdir		= famfs_rmdir,
	.rename		= famfs_rename,
	.forget		= famfs_forget,
	.forget_multi	= famfs_forget_multi,
	.getattr	= famfs_getattr,
	.setattr	= famfs_setattr,
	.readlink	= famfs_readlink,
	.opendir	= famfs_opendir,
	.readdir	= famfs_readdir,
	.readdirplus	= famfs_readdirplus,
	.releasedir	= famfs_releasedir,
	.fsyncdir	= famfs_fsyncdir,
	.create		= famfs_create,
	.open		= famfs_open,
	.release	= famfs_release,
	.flush		= famfs_flush,
	.fsync		= famfs_fsync,
	.read		= famfs_read,
	.write_buf      = famfs_write_buf,
	.statfs		= famfs_statfs,
	.fallocate	= famfs_fallocate,
	.flock		= famfs_flock,
	.getxattr	= famfs_getxattr,
	.listxattr	= famfs_listxattr,
	.setxattr	= famfs_setxattr,
	.removexattr	= famfs_removexattr,
#ifdef HAVE_COPY_FILE_RANGE
	.copy_file_range = famfs_copy_file_range,
#endif
	.lseek		= famfs_lseek,
};

void jg_print_fuse_opts(struct fuse_cmdline_opts *opts)
{
	printf("Cmdline opts:\n"
	       "  singlethread:      %d\n"
	       "  foreground:        %d\n"
	       "  debug:             %d\n"
	       "  nodefault_subtype: %d\n"
	       "  mount point:       %s\n"
	       "  clone_fd:          %d\n"
	       "  max_idle_threads;  %d\n"
	       "  max_threads:       %d\n",
	       opts->singlethread, opts->foreground, opts->debug,
	       opts->nodefault_subtype, opts->mountpoint,
	       opts->clone_fd, opts->max_idle_threads, opts->max_threads);
}

void
fused_syslog(
	enum fuse_log_level level,
	const char *fmt, va_list ap)
{
	sd_journal_printv(level, fmt, ap);
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config *config;
	struct famfs_data famfs_data = { .debug = 0,
	                      .writeback = 0 };
	int ret = -1;

	/* Don't mask creation mode, kernel already did that */
	umask(0);

	pthread_mutex_init(&famfs_data.mutex, NULL);
	famfs_data.root.next = famfs_data.root.prev = &famfs_data.root;
	famfs_data.root.fd = -1;
	famfs_data.cache = CACHE_NORMAL;

	fuse_set_log_func(fused_syslog);

	/*
	 * This gets opts (fuse_cmdline_opts)
	 * (This is a struct containing option fields)
	 */
	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		printf("fuse_cmdline_help()----------------------------------\n");
		fuse_cmdline_help();
		printf("fuse_lowlevel_help()----------------------------------\n");
		fuse_lowlevel_help();
		printf("passthrough_ll_help()----------------------------------\n");
		passthrough_ll_help();
		ret = 0;
		goto err_out1;
	} else if (opts.show_version) {
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	if (opts.mountpoint == NULL) {
		printf("usage: %s [options] <mountpoint>\n", argv[0]);
		printf("       %s --help\n", argv[0]);
		ret = 1;
		goto err_out1;
	}

	dump_fuse_args(&args);
	/*
	 * This parses famfs_data from 
	 */
	if (fuse_opt_parse(&args, &famfs_data, famfs_opts, NULL)== -1)
		return 1;

	famfs_data.debug = opts.debug;
	famfs_data.root.refcount = 2;
	if (famfs_data.source) {
		struct stat stat;
		int res;

		res = lstat(famfs_data.source, &stat);
		if (res == -1) {
			fuse_log(FUSE_LOG_ERR, "failed to stat source (\"%s\"): %m\n",
				 famfs_data.source);
			exit(1);
		}
		if (!S_ISDIR(stat.st_mode)) {
			fuse_log(FUSE_LOG_ERR, "source is not a directory\n");
			exit(1);
		}

	} else {
		famfs_data.source = strdup("/");
		if(!famfs_data.source) {
			fuse_log(FUSE_LOG_ERR, "fuse: memory allocation failed\n");
			exit(1);
		}
	}
	if (!famfs_data.timeout_set) {
		switch (famfs_data.cache) {
		case CACHE_NEVER:
			famfs_data.timeout = 0.0;
			break;

		case CACHE_NORMAL:
			famfs_data.timeout = 1.0;
			break;

		case CACHE_ALWAYS:
			famfs_data.timeout = 86400.0;
			break;
		}
	} else if (famfs_data.timeout < 0) {
		fuse_log(FUSE_LOG_ERR, "timeout is negative (%lf)\n",
			 famfs_data.timeout);
		exit(1);
	}

	famfs_data.root.fd = open(famfs_data.source, O_PATH);
	if (famfs_data.root.fd == -1) {
		fuse_log(FUSE_LOG_ERR, "open(\"%s\", O_PATH): %m\n",
			 famfs_data.source);
		exit(1);
	}

	/*
	 * this creates the fuse session
	 */
	se = fuse_session_new(&args, &famfs_oper, sizeof(famfs_oper), &famfs_data);
	if (se == NULL)
	    goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
	    goto err_out2;

	if (fuse_session_mount(se, opts.mountpoint) != 0)
	    goto err_out3;

	jg_print_fuse_opts(&opts);
	fuse_daemonize(opts.foreground);

	/* Block until ctrl+c or fusermount -u */
	if (opts.singlethread)
		ret = fuse_session_loop(se);
	else {
		config = fuse_loop_cfg_create();
		fuse_loop_cfg_set_clone_fd(config, opts.clone_fd);
		fuse_loop_cfg_set_max_threads(config, opts.max_threads);
		ret = fuse_session_loop_mt(se, config);
		fuse_loop_cfg_destroy(config);
		config = NULL;
	}

	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	if (famfs_data.root.fd >= 0)
		close(famfs_data.root.fd);

	free(famfs_data.source);
	return ret ? 1 : 0;
}
