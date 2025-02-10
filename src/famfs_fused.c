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

//#include "../fuse/passthrough_helpers.h"
#include "famfs_lib.h"
#include "famfs_fmap.h"
#include "fuse_kernel.h"

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
	struct famfs_log_file_meta *fmeta;
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
	int pass_yaml; /* pass the shadow yaml through */
	struct famfs_inode root; /* protected by ->mutex */
};

void
famfs_dump_opts(const struct famfs_data *fd)
{
	printf("%s:\n", __func__);
	printf("    debug=%d\n", fd->debug);
	printf("    writeback=%d\n", fd->writeback);
	printf("    flock=%d\n", fd->flock);
	printf("    xattr=%d\n", fd->xattr);
	printf("    source=%s\n", fd->source);
	printf("    timeout=%f\n", fd->timeout);
	printf("    cache=%d\n", fd->cache);
	printf("    timeout_set=%d\n", fd->timeout_set);
	printf("    pass_yaml=%d\n", fd->pass_yaml);

	fuse_log(FUSE_LOG_DEBUG, "%s:\n", __func__);
	fuse_log(FUSE_LOG_DEBUG, "    debug=%d\n", fd->debug);
	fuse_log(FUSE_LOG_DEBUG, "    writeback=%d\n", fd->writeback);
	fuse_log(FUSE_LOG_DEBUG, "    flock=%d\n", fd->flock);
	fuse_log(FUSE_LOG_DEBUG, "    xattr=%d\n", fd->xattr);
	fuse_log(FUSE_LOG_DEBUG, "    source=%s\n", fd->source);
	fuse_log(FUSE_LOG_DEBUG, "    timeout=%f\n", fd->timeout);
	fuse_log(FUSE_LOG_DEBUG, "    cache=%d\n", fd->cache);
	fuse_log(FUSE_LOG_DEBUG, "    timeout_set=%d\n", fd->timeout_set);
	fuse_log(FUSE_LOG_DEBUG, "    pass_yaml=%d\n", fd->pass_yaml);
}

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

static void famfs_fused_help(void)
{
	printf(
"    -o writeback           Enable writeback\n"
"    -o no_writeback        Disable write back\n"
"    -o source=/home/dir    Source directory to be mounted\n"
"    -o flock               Enable flock\n" //XXX always enable?
"    -o no_flock            Disable flock\n"
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

	if (conn->capable & FUSE_CAP_PASSTHROUGH)
		fuse_log(FUSE_LOG_NOTICE, "%s: Kernel is passthrough-capable\n", __func__);

	if (conn->capable & FUSE_CAP_DAX_IOMAP) {
		fuse_log(FUSE_LOG_NOTICE,  "%s: Kernel is DAX_IOMAP-capable\n", __func__);
		conn->want |= FUSE_CAP_DAX_IOMAP;
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
#if 1
	/*
	 * Setattr makes ephemeral changes to famfs. The authority is the metadata log.
	 * Still, we allow certain changes:
	 * * mode
	 * * uid, gid
	 * XXX how can we cache ephemeral changes without changing the yaml (which must
	 * reflect the log authority)?...
	 */
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
#else
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
#endif
}

static struct famfs_inode *famfs_find(struct famfs_data *lo, fuse_ino_t ino)
{
	/* TODO: replace this lookup mechanism with Bernd's wbtree lookup code */
	struct famfs_inode *p;
	struct famfs_inode *ret = NULL;

	pthread_mutex_lock(&lo->mutex);
	for (p = lo->root.next; p != &lo->root; p = p->next) {
		if (p->ino == ino) {
			assert(p->refcount > 0);
			ret = p;
			ret->refcount++;
			break;
		}
	}
	pthread_mutex_unlock(&lo->mutex);
	return ret;
}

#if 0
/* XXX TODO: go to statx, expose birth time */
void dump_stat(const struct stat *fileStat)
{
	char timeStr[100];
	struct tm *tm_info;

	printf("File Information:\n");
	printf("--------------------\n");
	printf("Device ID: %ld\n", (long)fileStat->st_dev);
	printf("Inode Number: %ld\n", (long)fileStat->st_ino);
	printf("Mode: 0%o\n", fileStat->st_mode);
	printf("Hard Links: %ld\n", (long)fileStat->st_nlink);
	printf("Owner: %ld\n", (long)fileStat->st_uid);
	printf("Group: %ld\n", (long)fileStat->st_gid);
	printf("File Size: %ld bytes\n", (long)fileStat->st_size);
	printf("Block Size: %ld\n", (long)fileStat->st_blksize);
	printf("Number of Blocks: %ld\n", (long)fileStat->st_blocks);

	tm_info = localtime(&fileStat->st_atime);
	strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", tm_info);
	printf("Last Access: %s\n", timeStr);

	tm_info = localtime(&fileStat->st_mtime);
	strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", tm_info);
	printf("Last Modification: %s\n", timeStr);

	tm_info = localtime(&fileStat->st_ctime);
	strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", tm_info);
	printf("Last Status Change: %s\n", timeStr);
}
#endif

void *
famfs_read_fd_to_buf(int fd, ssize_t max_size, ssize_t *size_out, int verbose)
{
	char *buf;
	ssize_t n;

	if (max_size > FAMFS_YAML_MAX)
		fuse_log(FUSE_LOG_ERR, "%s: max_size=%lld > limit=%d\n",
			 __func__, max_size, FAMFS_YAML_MAX);

	buf = calloc(1, max_size + 8);
	if (!buf) {
		fuse_log(FUSE_LOG_ERR, "%s: failed to malloc(%ld)\n", __func__, max_size);
		return NULL;
	}

	n = pread(fd, buf, max_size, 0);
	if (n < 0) {
		fuse_log(FUSE_LOG_ERR,
			 "%s: failed to read max_size=%ld from fd(%d) errno %d\n",
			 __func__, max_size, fd, errno);
		free(buf);
		*size_out = 0;
		return NULL;
	}
	*size_out = n;

	return buf;
}

static int
famfs_shadow_to_stat(
	void *yaml_buf,
	ssize_t bufsize,
	const struct stat *shadow_stat,
	struct stat *stat_out,
	struct famfs_log_file_meta *fmeta_out,
	int verbose)
{
	struct famfs_log_file_meta fmeta = {0};
	FILE *yaml_stream;
	int rc;

	assert(fmeta_out);
	if (bufsize < 100) /* This is imprecise... */
		fuse_log(FUSE_LOG_ERR,
			 "File size=%ld: too small  to contain valid yaml\n",
			 bufsize);

	if (verbose)
		fuse_log(FUSE_LOG_DEBUG, "file yaml:\n%s\n", (char *)yaml_buf);

	/* Make a stream for the yaml parser to use */
	yaml_stream = fmemopen((void *)yaml_buf, bufsize, "r");
	if (!yaml_stream) {
		fuse_log(FUSE_LOG_ERR,
			 "failed to convert yaml_buf to stream (errno=%d\n",
			 __func__, errno);
		return -1;
	}

	rc = famfs_parse_shadow_yaml(yaml_stream, &fmeta,
				     FAMFS_MAX_SIMPLE_EXTENTS,
				     FAMFS_MAX_SIMPLE_EXTENTS, verbose);
	if (rc) {
		fuse_log(FUSE_LOG_ERR, "%s: err from yaml parser rc=%d\n", __func__, rc);
		return rc;
	}

	/* Fields we don't provide */
	stat_out->st_dev     = shadow_stat->st_dev;
	stat_out->st_rdev    = shadow_stat->st_rdev;
	stat_out->st_blksize = shadow_stat->st_blksize;
	stat_out->st_blocks  = shadow_stat->st_blocks;

	/* Fields that come from the meta file stat */
	stat_out->st_atime = shadow_stat->st_atime;
	stat_out->st_mtime = shadow_stat->st_mtime;
	stat_out->st_ctime = shadow_stat->st_ctime;
	stat_out->st_ino   = shadow_stat->st_ino; /* Need a unique inode #; this is as good as any */

	/* Fields that come from the shadow yaml */
	stat_out->st_mode = fmeta.fm_mode | 0100000; /* octal; mark as regular file */
	stat_out->st_uid  = fmeta.fm_uid;
	stat_out->st_gid  = fmeta.fm_gid;
	stat_out->st_size = fmeta.fm_size;

	*fmeta_out = fmeta;

	fclose(yaml_stream);

	return 0;
}

#define FMAP_MSG_MAX 4096

int fuse_reply_famfs_entry(
	fuse_req_t req,
	const struct fuse_entry_param *e,
	const struct famfs_log_fmap *fmeta)
{
	char fmap_message[FMAP_MSG_MAX];
	ssize_t fmap_size;

	/* Dir lookup reply has no fmap */
	if (S_ISDIR(e->attr.st_mode))
		return fuse_reply_entry(req, e);

	/* XXX: correctly mark the log and superblock files (not as regular) */
	fmap_size = famfs_log_file_meta_to_msg(fmap_message, FMAP_MSG_MAX,
					       FUSE_FAMFS_FILE_REG, fmeta);
	if (fmap_size < 0) {
		/* Send reply without fmap */
		fuse_log(FUSE_LOG_ERR, "%s: %ld error putting fmap in message\n",
			 __func__, fmap_size);
		return fuse_reply_entry(req, e);
	}

	return fuse_reply_entry_plus(req, e, fmap_message, fmap_size);
}


static int
famfs_do_lookup(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	struct fuse_entry_param *e,
	struct famfs_log_file_meta **fmeta_out)
{
	struct famfs_log_file_meta *fmeta = NULL;
	struct famfs_data *lo = famfs_data(req);
	struct famfs_inode *inode;
	struct stat st;
	int parentfd;
	int saverr;
	int newfd;
	int res;

	fuse_log(FUSE_LOG_DEBUG, "%s: parent_inode=%lx name=%s\n",
		 __func__, parent, name);

	memset(e, 0, sizeof(*e));
	e->attr_timeout = lo->timeout;
	e->entry_timeout = lo->timeout;

	parentfd = famfs_fd(req, parent);

	fuse_log(FUSE_LOG_DEBUG, "%s: name=%s (%s)\n", __func__, name,
	       (parentfd < 0) ? "ERROR bad parentfd" : "good parentfd");
	newfd = openat(parentfd, name, O_PATH | O_NOFOLLOW, O_RDONLY);
	if (newfd == -1) {
		fuse_log(FUSE_LOG_ERR, "%s: open failed errno=%d\n", __func__, errno);
		goto out_err;
	}

	/* Gotta check if this is a file or directory */
	res = fstatat(newfd, "", &st, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		goto out_err;

	if (S_ISDIR(st.st_mode)) {
		fuse_log(FUSE_LOG_DEBUG, "               : this is a directory\n");
		e->attr = st;
	} else if (S_ISREG(st.st_mode)) {
#if 1
		void *yaml_buf;
		ssize_t yaml_size;

		fuse_log(FUSE_LOG_DEBUG, "               : this is a directory\n");

		/* XXX: close and reopen now that we know it's a regular file */
		close(newfd);
		/* famfs: drop O_PATH so we can read the shadow yaml contents */
		newfd = openat(parentfd, name, O_NOFOLLOW, O_RDONLY);
		if (newfd == -1) {
			goto out_err;
		}
		
		yaml_buf = famfs_read_fd_to_buf(newfd, FAMFS_YAML_MAX, &yaml_size, 1);
		if (!yaml_buf) {
			fuse_log(FUSE_LOG_ERR, "failed to read to yaml_buf\n");
			goto out_err;
		}

		fmeta = calloc(1, sizeof(*fmeta));
		if (!fmeta)
			goto out_err;

		/* Famfs populates the stat struct from the shadow yaml */
		res = famfs_shadow_to_stat(yaml_buf, yaml_size, &st, &e->attr, fmeta, 1);
		if (res)
			goto out_err;
#else
		/* This exposes the yaml files directly */
		e->attr = st;
#endif
	}

	inode = famfs_find(famfs_data(req), e->attr.st_ino);
	if (inode) {
		fuse_log(FUSE_LOG_DEBUG, "               : Inode already cached\n");
		close(newfd);
		newfd = -1;
		if (!inode->fmeta) {
			fuse_log(FUSE_LOG_ERR, "%s: null fmeta for ino=%ld; populating\n",
				 __func__, e->attr.st_ino);
			inode->fmeta = fmeta;
		} else {
			free(fmeta);
			fmeta = NULL;
		}
	} else {
		struct famfs_inode *prev, *next;

		fuse_log(FUSE_LOG_DEBUG, "               : Caching inode\n");
		saverr = ENOMEM;
		inode = calloc(1, sizeof(struct famfs_inode));
		if (!inode)
			goto out_err;

		inode->refcount = 1;
		inode->fd = newfd;
		inode->ino = e->attr.st_ino;
		inode->dev = e->attr.st_dev;
		inode->fmeta = fmeta;

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
	if (fmeta_out)
		*fmeta_out = fmeta;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "  %lli/%s -> %lli\n",
			(unsigned long long) parent, name, (unsigned long long) e->ino);

	return 0;

out_err:
	saverr = errno;
	if (newfd != -1)
		close(newfd);
	if (fmeta)
		free(fmeta);
	return saverr;
}

static void
famfs_lookup(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	struct famfs_log_file_meta *fmeta = NULL;
	struct fuse_entry_param e;
	int err;

	if (famfs_debug(req))
		fuse_log(FUSE_LOG_DEBUG, "famfs_lookup(parent=%" PRIu64 ", name=%s)\n",
			parent, name);

	err = famfs_do_lookup(req, parent, name, &e, &fmeta);
	if (err)
		fuse_reply_err(req, err);
	else
		fuse_reply_famfs_entry(req, &e, &fmeta->fm_fmap);
}

static void
famfs_get_fmap(
	fuse_req_t req,
	fuse_ino_t ino)
{
	char fmap_message[FMAP_MSG_MAX];
	struct famfs_inode *inode;
	struct iovec iov[1];
	ssize_t fmap_size;
	int err = 0;

	inode = famfs_find(famfs_data(req), ino);
	if (!inode) {
		fuse_log(FUSE_LOG_ERR, "%s: inode 0x%lx not found\n", __func__, ino);
		fuse_reply_err(req, EINVAL);
	}

	if (!inode->fmeta) {
		fuse_log(FUSE_LOG_ERR, "%s: no fmap on inode\n", __func__);
		err = ENOENT;
		goto out_err;
	}

	/* XXX: FUSE_FAMFS_FILE_REG - mark sb and log correctly */
	fmap_size = famfs_log_file_meta_to_msg(fmap_message, FMAP_MSG_MAX,
					       FUSE_FAMFS_FILE_REG,
					       &(inode->fmeta->fm_fmap));
	if (fmap_size < 0) {
		/* Send reply without fmap */
		fuse_log(FUSE_LOG_ERR, "%s: %ld error putting fmap in message\n",
			 __func__, fmap_size);
		err = EINVAL;
		goto out_err;
	}
	iov[0].iov_base = fmap_message;
	iov[0].iov_len = fmap_size;
	fuse_reply_iov(req, iov, 1);
	return;

out_err:
	fuse_reply_err(req, err);
}

static void
famfs_mknod(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode,
	dev_t rdev)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_fuse_mkdir(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_symlink(
	fuse_req_t req,
	const char *link,
	fuse_ino_t parent,
	const char *name)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_link(
	fuse_req_t req,
	fuse_ino_t ino,
	fuse_ino_t parent,
	const char *name)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_rmdir(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
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
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_unlink(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
unref_inode(
	struct famfs_data *lo,
	struct famfs_inode *inode,
	uint64_t n)
{
	if (!inode)
		return;

	fuse_log(FUSE_LOG_DEBUG, "%s: parent=%lx\n", __func__, inode);

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
		if (inode->fmeta)
			free(inode->fmeta);
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
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx\n", __func__, ino);

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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx size=%ld ofs=%ld\n",
		  __func__, ino, size, offset);

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
				err = famfs_do_lookup(req, ino, name, &e, NULL);
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
	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx size=%ld offset=%ld\n",
		  __func__, ino, size, offset);
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
	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx size=%ld offset=%ld\n",
		  __func__, ino, size, offset);
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
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx\n", __func__, ino);

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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx\n", __func__, ino);

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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx\n", __func__, ino);

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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx\n", __func__, ino);

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
		fuse_log(FUSE_LOG_DEBUG, "%s(ino=%" PRIu64 ", size=%zd, "
			 "off=%lu)\n", __func__, ino, size, (unsigned long) offset);

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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx\n", __func__, ino);

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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx\n", __func__, ino);

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
#if 1
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
#else
	int err = EOPNOTSUPP;
	(void) ino;

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx ofs=%lx len=%ld\n",
		  __func__, ino, offset, length);

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
#endif
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

	fuse_log(FUSE_LOG_DEBUG, "%s: inode=%lx op=%d\n", __func__, ino, op);

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
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_listxattr(
	fuse_req_t req,
	fuse_ino_t ino,
	size_t size)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
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
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_removexattr(
	fuse_req_t req,
	fuse_ino_t ino,
	const char *name)
{
	fuse_log(FUSE_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
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
	.get_fmap       = famfs_get_fmap,
#if 0
	.get_daxdev     = famfs_get_daxdev,
#endif
};

void jg_print_fuse_opts(struct fuse_cmdline_opts *opts)
{
	char *format_str = "Cmdline opts:\n"
	       "  singlethread:      %d\n"
	       "  foreground:        %d\n"
	       "  debug:             %d\n"
	       "  nodefault_subtype: %d\n"
	       "  mount point:       %s\n"
	       "  clone_fd:          %d\n"
	       "  max_idle_threads;  %d\n"
		"  max_threads:       %d\n";
	printf(format_str,
	       opts->singlethread, opts->foreground, opts->debug,
	       opts->nodefault_subtype, opts->mountpoint,
	       opts->clone_fd, opts->max_idle_threads, opts->max_threads);
	fuse_log(FUSE_LOG_DEBUG, format_str,
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

#define PROGNAME "famfs_fused"

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config *config;
	struct famfs_data famfs_data = { 0 };
	int ret = -1;

	/* Don't mask creation mode, kernel already did that */
	umask(0);

	/* Setup famfs defaults */
	pthread_mutex_init(&famfs_data.mutex, NULL);
	famfs_data.root.next = famfs_data.root.prev = &famfs_data.root;
	famfs_data.root.fd = -1;

	/* Default options */
	famfs_data.debug = 1; /* Temporary */
	famfs_data.writeback = 0;
	famfs_data.flock = 1; /* Need flock for log locking on master node */
	famfs_data.xattr = 0;
	famfs_data.cache = CACHE_NORMAL;
	famfs_data.pass_yaml = 1;

	/*fuse_set_log_func(fused_syslog); */
	fuse_log_enable_syslog("famfs", LOG_PID | LOG_CONS, LOG_DAEMON);

	fuse_log(FUSE_LOG_DEBUG,  "%s: this is debug(=%d)\n", PROGNAME, FUSE_LOG_DEBUG);
	fuse_log(FUSE_LOG_NOTICE, "%s: this is a NOTICE\n", PROGNAME);
	fuse_log(FUSE_LOG_ERR,    "%s: this is an err(=%d)\n", PROGNAME, FUSE_LOG_ERR);

	/*
	 * This gets opts (fuse_cmdline_opts)
	 * (This is a struct containing option fields)
	 * ->libfuse/lib/helper.c/fuse_parse_cmdline_312() (currently)
	 */
	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		printf("fuse_cmdline_help()----------------------------------\n");
		fuse_cmdline_help();
		printf("fuse_lowlevel_help()----------------------------------\n");
		fuse_lowlevel_help();
		printf("famfs_fused_help()----------------------------------\n");
		famfs_fused_help();
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
	 * This parses famfs_data from the -o opts
	 */
	if (fuse_opt_parse(&args, &famfs_data, famfs_opts, NULL)== -1)
		return 1;

	famfs_data.debug = opts.debug;
	famfs_data.root.refcount = 2;

	famfs_dump_opts(&famfs_data);

	if (famfs_data.source) {
		struct stat stat;
		int res;

		res = lstat(famfs_data.source, &stat);
		if (res == -1) {
			fprintf(stderr,
				"%s: failed to stat source (\"%s\"): %m\n",
				PROGNAME, famfs_data.source);
			fuse_log(FUSE_LOG_ERR,
				 "%s: failed to stat source (\"%s\"): %m\n",
				 PROGNAME, famfs_data.source);
			exit(1);
		}
		if (!S_ISDIR(stat.st_mode)) {
			fprintf(stderr, "%s: source (%s) is not a directory\n",
				PROGNAME, famfs_data.source);
			fuse_log(FUSE_LOG_ERR, "%s: source (%s) is not a directory\n",
				 PROGNAME, famfs_data.source);
			exit(1);
		}

		/* XXX validate that the source is a valid famfs shadow fs */

	} else {
		fuse_log(FUSE_LOG_ERR,
			 "%s: must supply shadow fs path as -o source=</shadow/path>\n",
			 PROGNAME);
		fprintf(stderr, 
			"%s: must supply shadow fs path as -o source=</shadow/path>\n",
			PROGNAME);
		exit(1);
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
	printf("root=(%s) fd=%d\n", famfs_data.source, famfs_data.root.fd);
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

	/* This daemonizes if !opts.foreground */
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

	fuse_log(FUSE_LOG_NOTICE, "%s: umount %s\n", PROGNAME, opts.mountpoint);
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
