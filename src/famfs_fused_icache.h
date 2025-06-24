// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

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

struct famfs_inode {
	struct famfs_inode *next; /* protected by lo->mutex */
	struct famfs_inode *prev; /* protected by lo->mutex */
	int fd;
	ino_t ino;
	dev_t dev;
	uint64_t refcount; /* protected by lo->mutex */
	struct famfs_log_file_meta *fmeta;
	struct famfs_data *owner;
};

struct famfs_icache {
	pthread_mutex_t mutex;
	struct famfs_inode root;
	uint64_t count;
};

static inline uint64_t
famfs_icache_count(struct famfs_icache *icache)
{
	return icache->count;
}

void famfs_icache_init(struct famfs_icache *icache);
void famfs_icache_destroy(struct famfs_icache *icache);
struct famfs_inode *famfs_inode_alloc(struct famfs_icache *icache,
				      void *owner, int fd,
				      ino_t inode_num, dev_t dev,
				      struct famfs_log_file_meta *fmeta);
struct famfs_inode *famfs_icache_find_locked(struct famfs_icache *icache,
					     uint64_t nodeid);
struct famfs_inode *famfs_icache_find(struct famfs_icache *icache,
				      uint64_t nodeid);
void famfs_icache_insert_locked(struct famfs_icache *icache,
				struct famfs_inode *inode);
void
famfs_icache_unref_inode(struct famfs_icache *icache, struct famfs_inode *inode,
			 void *owner, uint64_t n);


