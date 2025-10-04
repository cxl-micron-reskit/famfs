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
#include "fuse_i.h"
#include "famfs_fused_icache.h"

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

enum {
	CACHE_NEVER,
	CACHE_NORMAL,
	CACHE_ALWAYS,
};

struct famfs_ctx {
	int debug;
	int writeback;
	int flock;
	int xattr;
	char *source;
	char *daxdev;
	int max_daxdevs;
	struct famfs_daxdev *daxdev_table;
	double timeout;
	int cache;
	int timeout_set;
	int pass_yaml; /* pass the shadow yaml through */
	int readdirplus;
	struct famfs_icache icache;
};

void
famfs_dump_opts(const struct famfs_ctx *fd)
{
	printf("%s:\n", __func__);
	printf("    debug=%d\n", fd->debug);
	printf("    writeback=%d\n", fd->writeback);
	printf("    flock=%d\n", fd->flock);
	printf("    xattr=%d\n", fd->xattr);
	printf("    shadow=%s\n", fd->source);
	printf("    daxdev=%s\n", fd->daxdev);
	printf("    timeout=%f\n", fd->timeout);
	printf("    cache=%d\n", fd->cache);
	printf("    timeout_set=%d\n", fd->timeout_set);
	printf("    pass_yaml=%d\n", fd->pass_yaml);

	famfs_log(FAMFS_LOG_DEBUG, "%s:\n", __func__);
	famfs_log(FAMFS_LOG_DEBUG, "    debug=%d\n", fd->debug);
	famfs_log(FAMFS_LOG_DEBUG, "    writeback=%d\n", fd->writeback);
	famfs_log(FAMFS_LOG_DEBUG, "    flock=%d\n", fd->flock);
	famfs_log(FAMFS_LOG_DEBUG, "    xattr=%d\n", fd->xattr);
	famfs_log(FAMFS_LOG_DEBUG, "    shadow=%s\n", fd->source);
	famfs_log(FAMFS_LOG_DEBUG, "    daxdev=%s\n", fd->daxdev);
	famfs_log(FAMFS_LOG_DEBUG, "    timeout=%f\n", fd->timeout);
	famfs_log(FAMFS_LOG_DEBUG, "    cache=%d\n", fd->cache);
	famfs_log(FAMFS_LOG_DEBUG, "    timeout_set=%d\n", fd->timeout_set);
	famfs_log(FAMFS_LOG_DEBUG, "    pass_yaml=%d\n", fd->pass_yaml);
}

/*
 * These are the "-o" opts
 */
static const struct fuse_opt famfs_opts[] = {
	{ "writeback",
	  offsetof(struct famfs_ctx, writeback), 1 },
	{ "no_writeback",
	  offsetof(struct famfs_ctx, writeback), 0 },
	{ "shadow=%s",
	  offsetof(struct famfs_ctx, source), 0 },
	{ "source=%s",
	  offsetof(struct famfs_ctx, source), 0 }, /* opts source & shadow are same */
	{ "daxdev=%s",
	  offsetof(struct famfs_ctx, daxdev), 0 },
	{ "flock",
	  offsetof(struct famfs_ctx, flock), 1 },
	{ "no_flock",
	  offsetof(struct famfs_ctx, flock), 0 },
	{ "pass_yaml",
	  offsetof(struct famfs_ctx, pass_yaml), 0 },
	{ "timeout=%lf",
	  offsetof(struct famfs_ctx, timeout), 0 },
	{ "timeout=",
	  offsetof(struct famfs_ctx, timeout_set), 1 },
	{ "cache=never",
	  offsetof(struct famfs_ctx, cache), CACHE_NEVER },
	{ "cache=auto",
	  offsetof(struct famfs_ctx, cache), CACHE_NORMAL },
	{ "cache=always",
	  offsetof(struct famfs_ctx, cache), CACHE_ALWAYS },
	{ "readdirplus",
	  offsetof(struct famfs_ctx, readdirplus), 1 },
	{ "no_readdirplus",
	  offsetof(struct famfs_ctx, readdirplus), 0 },
	{ "debug=%d",
	  offsetof(struct famfs_ctx, debug), 0 },

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
"    -o source=/home/dir    Source directory to be mounted (required)\n"
"    -o shadow=/shadow/path Path to the famfs shadow tree\n"
"    -o daxdev=/dev/dax0.0  Devdax backing device\n"
"    -o flock               Enable flock\n" //XXX always enable?
"    -o no_flock            Disable flock\n"
"    -o timeout=1.0         Caching timeout\n"
"    -o timeout=0/1         Timeout is set\n"
"    -o cache=never         Disable cache\n"
"    -o cache=auto          Auto enable cache\n"
"    -o cache=always        Cache always\n");
}

static struct famfs_ctx *famfs_ctx_from_req(fuse_req_t req)
{
	return (struct famfs_ctx *) fuse_req_userdata(req);
}

static bool famfs_debug(fuse_req_t req)
{
	return famfs_ctx_from_req(req)->debug != 0;
}

static void famfs_init(
	void *userdata,
	struct fuse_conn_info *conn)
{
	struct famfs_ctx *lo = (struct famfs_ctx*) userdata;

	if (lo->writeback &&
	    conn->capable & FUSE_CAP_WRITEBACK_CACHE) {
		if (lo->debug)
			famfs_log(FAMFS_LOG_DEBUG,
				 "famfs_init: activating writeback\n");
		conn->want |= FUSE_CAP_WRITEBACK_CACHE;
	}
	if (lo->flock && conn->capable & FUSE_CAP_FLOCK_LOCKS) {
		if (lo->debug)
			famfs_log(FAMFS_LOG_DEBUG,
				 "famfs_init: activating flock locks\n");
		conn->want |= FUSE_CAP_FLOCK_LOCKS;
	}

	if (conn->capable & FUSE_CAP_PASSTHROUGH)
		famfs_log(FAMFS_LOG_NOTICE, "%s: Kernel is passthrough-capable\n",
			 __func__);

	if (conn->capable & FUSE_CAP_DAX_FMAP) {
		famfs_log(FAMFS_LOG_NOTICE,  "%s: Kernel is DAX_IOMAP-capable\n",
			 __func__);
		if (lo->daxdev) {
			famfs_log(FAMFS_LOG_NOTICE,
				 "%s: ENABLING DAX_IOMAP\n", __func__);
			conn->want |= FUSE_CAP_DAX_FMAP;
		} else {
			famfs_log(FAMFS_LOG_NOTICE,
				 "%s: disabling DAX_IOMAP (no daxdev)\n",
				 __func__);
		}
	}
}

static void famfs_destroy(void *userdata)
{
	struct famfs_ctx *lo = (struct famfs_ctx*) userdata;

	famfs_icache_destroy(&lo->icache);
}

static void
famfs_getattr(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct fuse_file_info *fi)
{
	struct famfs_ctx *lo = famfs_ctx_from_req(req);
	struct famfs_inode *inode = famfs_get_inode_from_nodeid(lo, nodeid);
	struct stat buf;
	int res;

	(void) fi;

	/*
	 * Root inode is a special case that is not looked up before getattr.
	 * All other indes have been looked up, and therefore already know
	 * their attrs
	 */
	if (nodeid == FUSE_ROOT_ID) {
		famfs_log(FAMFS_LOG_NOTICE, "%s: root inode\n", __func__);
		res = fstatat(inode->fd, "", &buf,
			      AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
		if (res == -1) {
			famfs_inode_putref(inode);
			return (void) fuse_reply_err(req, errno);
		}
		inode->attr = buf;
	}
	
	buf = inode->attr;
	famfs_inode_putref(inode);
	fuse_reply_attr(req, &buf, lo->timeout);
}

static void
famfs_setattr(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct stat *attr,
	int valid,
	struct fuse_file_info *fi)
{
	(void)nodeid;
	(void)attr;
	(void)valid;
	(void)fi;
	/*
	 * Setattr makes ephemeral changes to famfs. The authority is the
	 * metadata log.
	 * Still, we allow certain changes:
	 * * mode
	 * * uid, gid
	 * XXX how can we cache ephemeral changes without changing the yaml
	 * (which must reflect the log authority)?...
	 */
	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}


void *
famfs_read_fd_to_buf(int fd, ssize_t max_size, ssize_t *size_out)
{
	char *buf;
	ssize_t n;

	if (max_size > FAMFS_YAML_MAX)
		famfs_log(FAMFS_LOG_ERR, "%s: max_size=%lld > limit=%d\n",
			 __func__, max_size, FAMFS_YAML_MAX);

	buf = calloc(1, max_size + 8);
	if (!buf) {
		famfs_log(FAMFS_LOG_ERR, "%s: failed to malloc(%ld)\n",
			 __func__, max_size);
		return NULL;
	}

	n = pread(fd, buf, max_size, 0);
	if (n < 0) {
		famfs_log(FAMFS_LOG_ERR,
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
		famfs_log(FAMFS_LOG_ERR,
			 "File size=%ld: too small  to contain valid yaml\n",
			 bufsize);

	if (verbose)
		famfs_log(FAMFS_LOG_DEBUG, "file yaml:\n%s\n", (char *)yaml_buf);

	/* Make a stream for the yaml parser to use */
	yaml_stream = fmemopen((void *)yaml_buf, bufsize, "r");
	if (!yaml_stream) {
		famfs_log(FAMFS_LOG_ERR,
			 "failed to convert yaml_buf to stream (errno=%d\n",
			 __func__, errno);
		return -1;
	}

	rc = famfs_parse_shadow_yaml(yaml_stream, &fmeta,
				     FAMFS_MAX_SIMPLE_EXTENTS,
				     FAMFS_MAX_SIMPLE_EXTENTS, verbose);
	if (rc) {
		famfs_log(FAMFS_LOG_ERR, "%s: err from yaml parser rc=%d\n", __func__, rc);
		return rc;
	}

	famfs_log(FAMFS_LOG_NOTICE, "%s: ext_type=%d\n",
		 __func__, fmeta.fm_fmap.fmap_ext_type);

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

static int
famfs_check_inode(
	struct famfs_inode *inode,
	struct famfs_log_file_meta *fmeta,
	struct fuse_entry_param *e)
{
	(void)inode;
	(void)fmeta;
	(void)e;
	/* e->attr is struct stat */

	/* XXX make sure the inode and stat match as to the following:
	 * * Same type (file, directory, etc.)
	 * * Same fmap
	 * * What else?...
	 */
	return 0;
}

static int
famfs_do_lookup(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	struct fuse_entry_param *e,
	struct famfs_log_file_meta **fmeta_out)
{
	enum famfs_fuse_ftype ftype = FAMFS_FINVALID;
	struct famfs_log_file_meta *fmeta = NULL;
	struct famfs_ctx *lo = famfs_ctx_from_req(req);
	struct famfs_inode *parent_inode = famfs_get_inode_from_nodeid(lo,
								       parent);
	struct famfs_inode *inode = NULL;
	struct stat st;
	int parentfd;
	int saverr;
	int newfd;
	int res;

	famfs_log(FAMFS_LOG_DEBUG,
		 "%s: parent_inode=%ld name=%s icache_count=%lld\n",
		 __func__, parent, name, famfs_icache_count(&lo->icache));

	memset(e, 0, sizeof(*e));
	e->attr_timeout = lo->timeout;
	e->entry_timeout = lo->timeout;

	/* Note: this accesses the parent inode in our icache without looking
	 * it up. 'parent' is a pointer directly to the famfs_inode.
	 */
	parentfd = parent_inode->fd;

	famfs_log(FAMFS_LOG_DEBUG, "%s: name=%s (%s)\n", __func__, name,
	       (parentfd < 0) ? "ERROR bad parentfd" : "good parentfd");
	if (parentfd < 0)
		goto out_err;

	newfd = openat(parentfd, name, O_PATH | O_NOFOLLOW, O_RDONLY);
	if (newfd == -1) {
		famfs_log(FAMFS_LOG_ERR, "%s: open failed errno=%d\n",
			 __func__, errno);
		goto out_err;
	}

	/* Gotta check if this is a file or directory */
	res = fstatat(newfd, "", &st, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		goto out_err;

	e->attr = st;
	if (S_ISDIR(st.st_mode)) {
		famfs_log(FAMFS_LOG_DEBUG,
			 "               : inode=%d is a directory\n",
			 e->attr.st_ino);
		ftype = FAMFS_FDIR;
	} else if (S_ISREG(st.st_mode)) {
		ftype = FAMFS_FREG;

		if (lo->pass_yaml) {
			/* This exposes the yaml files directly */
			famfs_log(FAMFS_LOG_ERR,
				 "%s: passing yaml file stat (size=%ld)\n",
				 __func__, st.st_size);
			e->attr = st;
		} else {
			void *yaml_buf = NULL;
			ssize_t yaml_size;
			ino_t ino = st.st_ino; /* Inode number from file, not yaml */

			/* Now that we know it's a regular file, we must
			 * close and re-open without O_PATH to get to the
			 * shadow yaml */
			close(newfd);
			newfd = openat(parentfd, name, O_NOFOLLOW, O_RDONLY);
			if (newfd == -1) {
				goto out_err;
			}

			fmeta = calloc(1, sizeof(*fmeta));
			if (!fmeta)
				goto out_err;
		
			yaml_buf = famfs_read_fd_to_buf(newfd, FAMFS_YAML_MAX,
							&yaml_size);
			if (!yaml_buf) {
				famfs_log(FAMFS_LOG_ERR,
					 "failed to read to yaml_buf\n");
				goto out_err;
			}

			/* Don't keep regular files open - only directories */
			close(newfd);
			newfd = -1;

			/* Famfs gets the stat struct from the shadow yaml */
			res = famfs_shadow_to_stat(yaml_buf, yaml_size, &st,
						   &e->attr, fmeta, 0);
			if (res)
				goto out_err;
			st.st_ino = ino;

			if (yaml_buf)
				free(yaml_buf);
		}

		famfs_log(FAMFS_LOG_DEBUG, "               :"
			  "inode=%d size=%ld is a file\n",
			  e->attr.st_ino, e->attr.st_size);
	} else {
		famfs_log(FAMFS_LOG_DEBUG,
			 "               : inode=%d is neither file nor dir\n",
			 e->attr.st_ino);
		saverr = ENOENT;
		goto out_err;
	}

	inode = famfs_get_inode_from_nodeid(lo, e->attr.st_ino);
	if (!inode) {
		struct famfs_inode *inode2;

		famfs_log(FAMFS_LOG_DEBUG, "               : Caching inode %d\n",
			e->attr.st_ino);
		saverr = ENOMEM;

		inode = famfs_inode_alloc(&lo->icache, lo, newfd, name,
					  e->attr.st_ino,
					  e->attr.st_dev, fmeta, &e->attr,
					  ftype, parent_inode);

		if (!inode)
			goto out_err;

		/* We have an inode initialized. Need to take the lock and
		 * look one more time for (newly-inserted) existing inode
		 * to close a race. After we have the lock, if we find the
		 * inode in the cache, we'll discard the one we just allocated
		 */
		pthread_mutex_lock(&lo->icache.mutex);
		inode2 = famfs_get_inode_from_nodeid_locked(lo, e->attr.st_ino);
		if (inode2) {
			pthread_mutex_unlock(&lo->icache.mutex);
			famfs_log(FAMFS_LOG_ERR, "%s: CAUGHT THE INODE RACE!\n",
				 __func__);
			/* free inode, not put, it's not in the cache */
			famfs_inode_free(inode); 
			inode = inode2;
			goto found_inode;
		} else {
			famfs_icache_insert_locked(&lo->icache, inode);
		}
		pthread_mutex_unlock(&lo->icache.mutex);
	} else {
		int rc;
found_inode:

		famfs_log(FAMFS_LOG_DEBUG,
			 "               : inode=%d already cached\n",
			e->attr.st_ino);
		close(newfd);
		newfd = -1;
		rc = famfs_check_inode(inode, fmeta, e);
		if (rc) {
			/* Recover by replacing the stale metadata... */
			if (inode->fmeta) {
				free(inode->fmeta);
				inode->fmeta = NULL;
			}
		}
		if (!inode->fmeta && S_ISREG(st.st_mode)) {
			famfs_log(FAMFS_LOG_ERR,
				 "%s: null fmeta for ino=%ld; populating\n",
				 __func__, e->attr.st_ino);
			inode->fmeta = fmeta;
		} else {
			/* XXX: should we verify that fmeta matches inode? */
			free(fmeta);
			fmeta = NULL;
		}
	}

	/* The address of the famfs_inode is a valid "nodeid" because it is
	 * unique */
	e->ino = (uintptr_t) inode;
	if (fmeta_out)
		*fmeta_out = inode->fmeta;

	/* Note that the "nodeid" is used in-kernel, as fi->nodeid. It is the
	 * "key" used for looking up the famfs_inode. The inode number
	 * (attr.st_ino) is used as fi->inode->i_ino in the kernel, but it also
	 * remembers the nodeid (fi->nodeid); the kernel wants to lookup inodes
	 * by "nodeid".
	 *
	 * It would be tempting to think we don't need to call
	 * famfs_icache_find() since the nodeid is the address...but the
	 * inode might have been forgotten", in which case that memory will
	 * have been freed and/or reused. So we still have to look up the inode
	 * in our cache...
	 */
	famfs_log(FAMFS_LOG_NOTICE, "%s: nodeid=%lx i_ino=%ld ext_type=%d\n",
		 __func__, (long long)e->ino, e->attr.st_ino,
		  (inode->fmeta) ? (int)inode->fmeta->fm_fmap.fmap_ext_type : -1);

	if (famfs_debug(req))
		famfs_log(FAMFS_LOG_DEBUG, "  %lli/%s -> %lli\n",
			(unsigned long long) parent, name, (unsigned long long) e->ino);

	/* TODO: a vectorized famfs_inode_putref would be nice */
	if (parent_inode)
		famfs_inode_putref(parent_inode);
	if (inode)
		famfs_inode_putref(inode);
	return 0;

out_err:
	if (parent_inode)
		famfs_inode_putref(parent_inode);
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
		famfs_log(FAMFS_LOG_DEBUG, "famfs_lookup(parent=%" PRIu64 ", name=%s)\n",
			parent, name);

	err = famfs_do_lookup(req, parent, name, &e, &fmeta);
	if (err)
		fuse_reply_err(req, err);
	else
		fuse_reply_entry(req, &e);
}

static void
famfs_get_fmap(
	fuse_req_t req,
	fuse_ino_t nodeid,
	size_t size)
{
	struct famfs_ctx *lo = famfs_ctx_from_req(req);
	ssize_t fmap_bufsize = FMAP_MSG_MAX;
	struct famfs_inode *inode = NULL;
	char *fmap_message = NULL;
	ssize_t fmap_size;
	int err = 0;

	fmap_message = calloc(1, fmap_bufsize);
	if (!fmap_message) {
		err = ENOMEM;
		goto out_err;
	}
	
	famfs_log(FAMFS_LOG_NOTICE, "%s: inode=%ld size=%ld\n",
		 __func__, nodeid, size);

	/* The fuse v1 patch set uses the inode number as the nodeid, meaning
	 * we have to search the list every time. */
	inode = famfs_get_inode_from_nodeid(lo, nodeid);
	if (inode) /* XXX drop when first fuse patch set is deprecated */
		famfs_log(FAMFS_LOG_DEBUG, "%s: old kmod - found by i_ino\n",
			 __func__);

	/* If it's the v2 or later kmod, the nodeid is the address of the
	 * famfs_inode. Retrieving it this way validates that there is indeed
	 * an inode at that address.
	 */
	if (!inode)
		inode = famfs_get_inode_from_nodeid(famfs_ctx_from_req(req), nodeid);

	if (!inode) {
		famfs_log(FAMFS_LOG_ERR, "%s: inode 0x%ld not found\n",
			 __func__, nodeid);
		err = EINVAL;
		goto out_err;
	}

	if (!inode->fmeta) {
		famfs_log(FAMFS_LOG_ERR, "%s: no fmap on inode\n", __func__);
		err = ENOENT;
		goto out_err;
	}

	/* XXX: FUSE_FAMFS_FILE_REG - mark sb and log correctly */
	fmap_size = famfs_log_file_meta_to_msg(fmap_message, fmap_bufsize,
					       FUSE_FAMFS_FILE_REG,
					       inode->fmeta);
	if (fmap_size <= 0) {
		/* Send reply without fmap */
		famfs_log(FAMFS_LOG_ERR,
			  "%s: %ld error putting fmap in message\n",
			 __func__, fmap_size);
		err = EINVAL;
		goto out_err;
	}
	famfs_log(FAMFS_LOG_NOTICE, "%s: sending fmap message len=%ld\n",
		 __func__, fmap_size);
#if 1
	/* XXX revertme
	 * For the moment we return fmap_bufsize because the v1 famfs-fuse
	 * kernel patch set doesn't handle shorter replies. Revert this when
	 * we no longer need to support shortening the reply to the actual
	 * length of the fmap
	 */
	fmap_size = fmap_bufsize; /* override to 4K for v1 famfs-fuse kernel */
#endif
	err = fuse_reply_buf(req, fmap_message, fmap_size /* FMAP_MSG_MAX */);
	if (err)
		famfs_log(FAMFS_LOG_ERR, "%s: fuse_reply_buf returned err %d\n",
			 __func__, err);

	if (fmap_message)
		free(fmap_message);

	famfs_inode_putref(inode);
	return;

out_err:
	if (inode)
		famfs_inode_putref(inode);

	if (fmap_message)
		free(fmap_message);

	fuse_reply_err(req, err);
}

static void
famfs_get_daxdev(
	fuse_req_t req,
	int daxdev_index)
{
	struct famfs_ctx *fd = famfs_ctx_from_req(req);
	struct fuse_daxdev_out daxdev;
	int err = 0;

	famfs_log(FAMFS_LOG_NOTICE, "%s: daxdev_index=%d\n",
		 __func__, daxdev_index);
	memset(&daxdev, 0, sizeof(daxdev));

	/* Fill in daxdev struct */
	if (daxdev_index != 0) {
		famfs_log(FAMFS_LOG_ERR, "%s: non-zero daxdev index\n", __func__);
		err = EINVAL;
		goto out_err;
	}
	if (!fd->daxdev) {
		famfs_log(FAMFS_LOG_ERR, "%s: dax not enabled\n", __func__);
		err = EOPNOTSUPP;
		goto out_err;
	}

	/* Right now we can only retrieve index 0... */
	daxdev.index = 0;
	//daxdev.valid = 1;
	strncpy(daxdev.name, fd->daxdev_table[daxdev_index].dd_daxdev,
		FAMFS_DEVNAME_LEN - 1);

	err = fuse_reply_buf(req, (void *)&daxdev, sizeof(daxdev));
	if (err)
		famfs_log(FAMFS_LOG_ERR, "%s: fuse_reply_buf returned err %d\n",
			 __func__, err);
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
	(void)req;
	(void)parent;
	(void)name;
	(void)mode;
	(void)rdev;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_fuse_mkdir(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name,
	mode_t mode)
{
	(void)parent;
	(void)name;
	(void)mode;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_symlink(
	fuse_req_t req,
	const char *link,
	fuse_ino_t parent,
	const char *name)
{
	(void)link;
	(void)parent;
	(void)name;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_link(
	fuse_req_t req,
	fuse_ino_t nodeid,
	fuse_ino_t parent,
	const char *name)
{
	(void)nodeid;
	(void)parent;
	(void)name;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_rmdir(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	(void)parent;
	(void)name;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
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
	(void)parent;
	(void)name;
	(void)newparent;
	(void)newname;
	(void)flags;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_unlink(
	fuse_req_t req,
	fuse_ino_t parent,
	const char *name)
{
	(void)parent;
	(void)name;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_forget_one(
	fuse_req_t req,
	fuse_ino_t nodeid,
	uint64_t nlookup)
{
	struct famfs_ctx *lo = famfs_ctx_from_req(req);
	struct famfs_inode *inode = famfs_get_inode_from_nodeid(famfs_ctx_from_req(req),
							    nodeid);

	if (famfs_debug(req)) {
		famfs_log(FAMFS_LOG_DEBUG, "  forget %lli %lli -%lli\n",
			(unsigned long long) nodeid,
			(unsigned long long) inode->refcount,
			(unsigned long long) nlookup);
	}

	/* +1 because we got a ref when we looked it up here */
	famfs_icache_unref_inode(&lo->icache, inode, lo, nlookup + 1);
}

static void
famfs_forget(
	fuse_req_t req,
	fuse_ino_t nodeid,
	uint64_t nlookup)
{
	famfs_log(FAMFS_LOG_DEBUG, "%s:\n", __func__);
	famfs_forget_one(req, nodeid, nlookup);
	fuse_reply_none(req);
}

static void
famfs_forget_multi(
	fuse_req_t req,
	size_t count,
	struct fuse_forget_data *forgets)
{
	size_t i;

	famfs_log(FAMFS_LOG_DEBUG, "%s:\n", __func__);

	for (i = 0; i < count; i++)
		famfs_forget_one(req, forgets[i].ino, forgets[i].nlookup);
	fuse_reply_none(req);
}

static void
famfs_readlink(
	fuse_req_t req,
	fuse_ino_t nodeid)
{
	(void)nodeid;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
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
	fuse_ino_t nodeid,
	struct fuse_file_info *fi)
{
	int error = ENOMEM;
	struct famfs_ctx *lo = famfs_ctx_from_req(req);
	struct famfs_inode *inode = famfs_get_inode_from_nodeid(lo, nodeid);
	struct famfs_dirp *d;
	int fd;

	famfs_log(FAMFS_LOG_DEBUG, "%s: inode=%ld (%jx)\n",
		 __func__, nodeid, nodeid);

	d = calloc(1, sizeof(struct famfs_dirp));
	if (d == NULL)
		goto out_err;

	fd = openat(inode->fd, ".", O_RDONLY);
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
	famfs_inode_putref(inode);
	return;

out_errno:
	error = errno;
out_err:
	famfs_inode_putref(inode);
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
	fuse_ino_t nodeid,
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

	(void) nodeid;

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx size=%ld ofs=%ld plus=%d\n",
		 __func__, nodeid, size, offset, plus);

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
				err = famfs_do_lookup(req, nodeid,
						      name, &e, NULL);
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
	fuse_ino_t nodeid,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi)
{
	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx size=%ld offset=%ld\n",
		  __func__, nodeid, size, offset);
	famfs_do_readdir(req, nodeid, size, offset, fi, 0);
}

static void
famfs_releasedir(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct fuse_file_info *fi)
{
	struct famfs_dirp *d = famfs_dirp(fi);
	(void) nodeid;
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
	(void)parent;
	(void)name;
	(void)mode;
	(void)fi;

	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, ENOTSUP);
}

static void
famfs_fsyncdir(
	fuse_req_t req,
	fuse_ino_t nodeid,
	int datasync,
	struct fuse_file_info *fi)
{
	int res;
	int fd = dirfd(famfs_dirp(fi)->dp);
	(void) nodeid;
	if (datasync)
		res = fdatasync(fd);
	else
		res = fsync(fd);
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_open(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct fuse_file_info *fi)
{
	int fd;
	struct famfs_ctx *lo = famfs_ctx_from_req(req);
	struct famfs_inode *inode = famfs_get_inode_from_nodeid(lo, nodeid);
	struct famfs_inode *parent_inode = inode->parent;

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx\n", __func__, nodeid);

	if (famfs_debug(req))
		famfs_log(FAMFS_LOG_DEBUG, "famfs_open(nodeid=%lx, flags=%d)\n",
			nodeid, fi->flags);

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
#if 0
	sprintf(buf, "/proc/self/fd/%i", inode->fd);
	fd = open(buf, fi->flags & ~O_NOFOLLOW);
#else
	/* Note we're opening the shadow yaml, not the actual file. The
	 * the data for the actual file is accessed by the kernel component
	 * via the fmap. Might not need to actually open anything here...
	 */
	fd = openat(parent_inode->fd, inode->name, fi->flags & ~O_NOFOLLOW,
		    O_RDONLY /* don't make yaml writable in any case */);
#endif
	if (fd == -1) {
		famfs_inode_putref(inode);
		return (void) fuse_reply_err(req, errno);
	}

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

	famfs_inode_putref(inode);
	fuse_reply_open(req, fi);
}

static void
famfs_release(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct fuse_file_info *fi)
{
	(void) nodeid;

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx\n", __func__, nodeid);

	close(fi->fh);
	fuse_reply_err(req, 0);
}

static void
famfs_flush(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct fuse_file_info *fi)
{
	int res;
	(void) nodeid;

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx\n", __func__, nodeid);

	res = close(dup(fi->fh));
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_fsync(
	fuse_req_t req,
	fuse_ino_t nodeid,
	int datasync,
	struct fuse_file_info *fi)
{
	int res = 0;
	(void) nodeid;
	(void) datasync;
	(void) fi;

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx\n", __func__, nodeid);
#if 0
	if (datasync)
		res = fdatasync(fi->fh);
	else
		res = fsync(fi->fh);
#endif
	fuse_reply_err(req, res == -1 ? errno : 0);
}

static void
famfs_read(
	fuse_req_t req,
	fuse_ino_t nodeid,
	size_t size,
	off_t offset,
	struct fuse_file_info *fi)
{
	struct fuse_bufvec buf = FUSE_BUFVEC_INIT(size);

	if (famfs_debug(req))
		famfs_log(FAMFS_LOG_DEBUG, "%s(nodeid=%lx, size=%zd, off=%lu)\n",
			 __func__, nodeid, size, (unsigned long) offset);

	buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.buf[0].fd = fi->fh;
	buf.buf[0].pos = offset;

	fuse_reply_data(req, &buf, FUSE_BUF_SPLICE_MOVE);
}

static void
famfs_write_buf(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct fuse_bufvec *in_buf,
	off_t off,
	struct fuse_file_info *fi)
{
	(void) nodeid;
	ssize_t res;
	struct fuse_bufvec out_buf = FUSE_BUFVEC_INIT(fuse_buf_size(in_buf));

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx\n", __func__, nodeid);

	out_buf.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	out_buf.buf[0].fd = fi->fh;
	out_buf.buf[0].pos = off;

	if (famfs_debug(req))
		famfs_log(FAMFS_LOG_DEBUG,
			 "famfs_write(nodeid=%lx, size=%zd, off=%lu)\n",
			 nodeid, out_buf.buf[0].size, (unsigned long) off);

	res = fuse_buf_copy(&out_buf, in_buf, 0);
	if(res < 0)
		fuse_reply_err(req, -res);
	else
		fuse_reply_write(req, (size_t) res);
}

static void
famfs_statfs(
	fuse_req_t req,
	fuse_ino_t nodeid)
{
	int res;
	struct statvfs stbuf;
	struct famfs_ctx *lo = famfs_ctx_from_req(req);
	struct famfs_inode *inode = famfs_get_inode_from_nodeid(lo, nodeid);

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx\n", __func__, nodeid);

	res = fstatvfs(inode->fd, &stbuf);
	famfs_inode_putref(inode);
	if (res == -1)
		fuse_reply_err(req, errno);
	else
		fuse_reply_statfs(req, &stbuf);
	
}

static void
famfs_fallocate(
	fuse_req_t req,
	fuse_ino_t nodeid,
	int mode,
	off_t offset,
	off_t length,
	struct fuse_file_info *fi)
{
	(void)nodeid;
	(void)mode;
	(void)offset;
	(void)length;
	(void)fi;
	famfs_log(FAMFS_LOG_DEBUG, "%s: ENOTSUP\n", __func__);
	fuse_reply_err(req, EOPNOTSUPP);
}

static void
famfs_flock(
	fuse_req_t req,
	fuse_ino_t nodeid,
	struct fuse_file_info *fi,
	int op)
{
	int res;
	(void) nodeid;

	famfs_log(FAMFS_LOG_DEBUG, "%s: nodeid=%lx op=%d\n",
		 __func__, nodeid, op);

	res = flock(fi->fh, op);

	fuse_reply_err(req, res == -1 ? errno : 0);
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
		famfs_log(FAMFS_LOG_DEBUG, "famfs_copy_file_range(ino=%" PRIu64 "/fd=%lu, "
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
	fuse_ino_t nodeid,
	off_t off,
	int whence,
	struct fuse_file_info *fi)
{
	off_t res;

	(void)nodeid;
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
	.forget		= famfs_forget,
	.getattr	= famfs_getattr,
	.setattr	= famfs_setattr,
	.readlink	= famfs_readlink,
	.mknod		= famfs_mknod,
	.mkdir		= famfs_fuse_mkdir,
	.unlink		= famfs_unlink,
	.rmdir		= famfs_rmdir,
	.symlink	= famfs_symlink,
	.rename		= famfs_rename,
	.link		= famfs_link,
	.open		= famfs_open,
	.read		= famfs_read,
	/* .write */
	.flush		= famfs_flush,
	.release	= famfs_release,
	.fsync		= famfs_fsync,
	.opendir	= famfs_opendir,
	.readdir	= famfs_readdir,
	.releasedir	= famfs_releasedir,
	.fsyncdir	= famfs_fsyncdir,
	.statfs		= famfs_statfs,
	/* .setxattr */
	/* .getxattr */
	/* .listxattr */
	/* .removexattr */
	/* .access */
	.create		= famfs_create,
	/* .getlk */
	/* .setlk */
	/* .ioctl */
	/* .poll */
	.write_buf      = famfs_write_buf,
	/* .retrieve_reply */
	.forget_multi	= famfs_forget_multi,
	.flock		= famfs_flock,
	.fallocate	= famfs_fallocate,
	//.readdirplus	= famfs_readdirplus,
#ifdef HAVE_COPY_FILE_RANGE
	.copy_file_range = famfs_copy_file_range,
#endif
	.lseek		= famfs_lseek,
	.get_fmap       = famfs_get_fmap,
	.get_daxdev     = famfs_get_daxdev,
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
	famfs_log(FAMFS_LOG_DEBUG, format_str,
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

#define MAX_DAXDEVS 1

struct famfs_ctx famfs_context;

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct famfs_ctx *lo = &famfs_context;
	struct fuse_loop_config *config;
	struct fuse_cmdline_opts opts;
	char *shadow_root = NULL;
	struct fuse_session *se;
	char shadow_opt[80];
	int ret = -1;

	/* Don't mask creation mode, kernel already did that */
	umask(0);

	/* Default options */
	lo->debug = 1; /* Temporary */
	lo->writeback = 0;
	lo->flock = 1; /* Need flock for log locking on master node */
	lo->xattr = 0;
	lo->cache = CACHE_NORMAL;
	lo->pass_yaml = 0;

	/*fuse_set_log_func(fused_syslog); */
	fuse_log_enable_syslog("famfs", LOG_PID | LOG_CONS, LOG_DAEMON);
	famfs_log_set_default_log_level(FAMFS_LOG_DEBUG);
	
	famfs_log(FAMFS_LOG_DEBUG,  "%s: this is debug(=%d)\n",
		 PROGNAME, FUSE_LOG_DEBUG);
	famfs_log(FAMFS_LOG_ERR,    "%s: this is an err(=%d)\n",
		 PROGNAME, FUSE_LOG_ERR);

	/*
	 * This gets opts (fuse_cmdline_opts)
	 * (This is a struct containing option fields)
	 * ->libfuse/lib/helper.c/fuse_parse_cmdline_312() (currently)
	 */
	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		printf("fuse_cmdline_help()--------------------------------\n");
		fuse_cmdline_help();
		printf("fuse_lowlevel_help()-------------------------------\n");
		fuse_lowlevel_help();
		printf("famfs_fused_help()---------------------------------\n");
		famfs_fused_help();
		ret = 0;
		goto err_out1;
	} else if (opts.show_version) {
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	dump_fuse_args(&args);

	if (opts.mountpoint == NULL) {
		printf("usage: %s [options] <mountpoint>\n", argv[0]);
		printf("       %s --help\n", argv[0]);
		ret = 1;
		goto err_out1;
	}

	/*
	 * This parses famfs_context from the -o opts
	 */
	if (fuse_opt_parse(&args, &famfs_data, famfs_opts, NULL)== -1) {
		ret = -1;
		goto err_out1;
	}

	lo->debug = opts.debug;

	famfs_log(FAMFS_LOG_NOTICE, "famfs mount shadow=%s mpt=%s\n",
		 lo->source, opts.mountpoint);

	famfs_dump_opts(&famfs_context);

	if (lo->daxdev) {
		/* Store the primary daxdev in slot 0 of the daxdev_table... */
		lo->daxdev_table =
			calloc(MAX_DAXDEVS, sizeof(*lo->daxdev_table));
		strncpy(lo->daxdev_table[0].dd_daxdev,
			lo->daxdev, FAMFS_DEVNAME_LEN - 1);
	}

	if (!lo->source) {
		const char *fmt = "%s: must supply shadow fs path "
			"as -o source=</shadow/path>\n";

		famfs_log(FAMFS_LOG_ERR, fmt, PROGNAME);
		fprintf(stderr, fmt, PROGNAME);
		ret = 1;
		goto err_out1;
	}

	shadow_root = famfs_get_shadow_root(lo->source, 0 /* verbose */);
	if (!shadow_root) {
		fprintf(stderr, "%s: failed to resolve shadow_root from %s\n",
			__func__, lo->source);
		goto err_out1;
	}

	if (!lo->timeout_set) {
		switch (lo->cache) {
		case CACHE_NEVER:
			lo->timeout = 0.0;
			break;

		case CACHE_NORMAL:
			lo->timeout = 1.0;
			break;

		case CACHE_ALWAYS:
			lo->timeout = 86400.0;
			break;
		}
	} else if (lo->timeout < 0) {
		famfs_log(FAMFS_LOG_ERR, "timeout is negative (%lf)\n",
			 lo->timeout);
		ret = 1;
		goto err_out1;
	}
	printf("timeout=%f\n", lo->timeout);

	ret = famfs_icache_init(&famfs_context, &lo->icache, shadow_root);
	if (ret) {
		free(shadow_root);
		ret = 1;
		goto err_out1;
	}

	/*
	 * this creates the fuse session
	 */
	se = fuse_session_new(&args, &famfs_oper, sizeof(famfs_oper),
			      &famfs_context);
	if (se == NULL)
	    goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
	    goto err_out2;

	/* Add shadow arg to kernel mount opts */
	snprintf(shadow_opt, sizeof(shadow_opt), "shadow=%s", lo->source);
	if (fuse_add_kernel_mount_opt(se, shadow_opt))
		famfs_log(FAMFS_LOG_ERR,
			 "%s: failed to add kernel mount opt (%s)\n",
			 __func__, shadow_opt);

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

	famfs_log(FAMFS_LOG_NOTICE, "%s: umount %s\n", PROGNAME, opts.mountpoint);
	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	if (shadow_root)
		free(shadow_root);
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	famfs_icache_destroy(&lo->icache);

	if (lo->daxdev_table)
		free(lo->daxdev_table);

	free(lo->source);
	return ret ? 1 : 0;
}
