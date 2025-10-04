// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

#ifndef FAMFS_FUSED_ICACHE
#define FAMFS_FUSED_ICACHE

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

#include "famfs_lib.h"
#include "fuse_log.h"

enum famfs_fuse_ftype {
	FAMFS_FREG,
	FAMFS_FDIR,
	FAMFS_FINVALID,
};

/* flags */

#define FAMFS_ROOTDIR 1

struct famfs_icache;

struct famfs_inode {
	struct famfs_inode *next;          /* protected by lo->mutex */
	struct famfs_inode *prev;          /* protected by lo->mutex */
	int fd;                            /* fd must be closed if > 0 */
	ino_t ino;
	dev_t dev;
	int flags;
	uint64_t refcount;                 /* protected by lo->mutex */
	struct famfs_icache *icache;
	struct famfs_log_file_meta *fmeta; /* fmeta must be freed */
	struct stat attr;
	enum famfs_fuse_ftype ftype;
	struct famfs_inode *parent;        /* parent ref must be dropped */
	char *name;                        /* name must be freed */
};

struct famfs_icache {
	pthread_mutex_t mutex;
	struct famfs_inode root;
	uint64_t count;
	char *shadow_root;
	void *owner;
};

static inline uint64_t
famfs_icache_count(struct famfs_icache *icache)
{
	return icache->count;
}

int famfs_icache_init(
	void *owner,
	struct famfs_icache *icache,
	const char *shadow_root);
void famfs_icache_destroy(struct famfs_icache *icache);
struct famfs_inode *famfs_inode_alloc(
	struct famfs_icache *icache, int fd,
	const char *name, ino_t inode_num, dev_t dev,
	struct famfs_log_file_meta *fmeta, struct stat *attrp,
	enum famfs_fuse_ftype ftype, struct famfs_inode *parent);

void famfs_icache_insert_locked(struct famfs_icache *icache,
				struct famfs_inode *inode);
void
famfs_icache_unref_inode(struct famfs_icache *icache, struct famfs_inode *inode,
			 uint64_t n);
void dump_icache(struct famfs_icache *icache);

struct famfs_inode *famfs_get_inode_from_nodeid(
	struct famfs_icache *icache, fuse_ino_t nodeid);
struct famfs_inode *famfs_get_inode_from_nodeid_locked(
	struct famfs_icache *icache, fuse_ino_t nodeid);

struct famfs_inode *famfs_icache_find_get_from_ino_locked(
	struct famfs_icache *icache, uint64_t ino);
struct famfs_inode *famfs_icache_find_get_from_ino(
	struct famfs_icache *icache, uint64_t ino);

static inline void famfs_inode_getref_locked(struct famfs_inode *inode)
{
	if (inode)
		inode->refcount++;
};
void famfs_inode_free(struct famfs_inode *inode);

static inline void famfs_inode_getref(
	struct famfs_icache *icache,
	struct famfs_inode *inode)
{
	pthread_mutex_lock(&icache->mutex);
	famfs_inode_getref_locked(inode);
	pthread_mutex_unlock(&icache->mutex);
};

void famfs_inode_putref_locked(struct famfs_inode *inode);
void famfs_inode_putref(struct famfs_inode *inode);


#endif /* FAMFS_FUSED_ICACHE */
