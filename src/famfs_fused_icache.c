// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

#include "famfs_fused_icache.h"

void famfs_icache_init(struct famfs_icache *icache)
{
	pthread_mutex_init(&icache->mutex, NULL);
	icache->root.next = icache->root.prev = &icache->root;

	icache->root.fd = -1;
}

void famfs_icache_destroy(struct famfs_icache *icache)
{
	while (icache->root.next != &icache->root) {
		struct famfs_inode* next = icache->root.next;

		icache->root.next = next->next;
		close(next->fd);
		free(next);
	}
}

void dump_icache(struct famfs_icache *icache)
{
	struct famfs_inode *p;
	size_t nino = 0;

	pthread_mutex_lock(&icache->mutex);
	famfs_log(FAMFS_LOG_DEBUG, "%s: count=%lld\n",
		 __func__, icache->count);
	for (p = icache->root.next; p != &icache->root; p = p->next) {
		famfs_log(FAMFS_LOG_DEBUG, "\tinode=%ld\n", p->ino);
		nino++;
	}
	pthread_mutex_unlock(&icache->mutex);
	famfs_log(FAMFS_LOG_DEBUG, "   %ld inodes cached\n", nino);
}

/*
 * Move to new: famfs_icache.c
 */

struct famfs_inode *
famfs_inode_alloc(
	struct famfs_icache *icache,
	void *owner,
	int fd,
	ino_t inode_num,
	dev_t dev,
	struct famfs_log_file_meta *fmeta)
{
	struct famfs_inode *inode;
	(void)icache;

	inode = calloc(1, sizeof(*inode));
	if (!inode)
		return NULL;

	inode->owner = owner;
	inode->refcount = 1;

	inode->fd = fd;
	inode->ino = inode_num;
	inode->dev = dev;
	inode->fmeta = fmeta;

	return inode;
}

/**
 * famfs_find_inode_locked(): find a cached famfs_inode
 *
 * Caller must hold the mutex
 */
struct famfs_inode *
famfs_icache_find_locked(struct famfs_icache *icache, uint64_t nodeid)
{
	/* TODO: replace this lookup mechanism with Bernd's wbtree lookup code */
	struct famfs_inode *p;
	struct famfs_inode *ret = NULL;

	for (p = icache->root.next; p != &icache->root; p = p->next) {
		/* Nodeid is the address of the entry we're looking for */
		if (p->ino == nodeid) {
			assert(p->refcount > 0);
			ret = p;
			ret->refcount++;
			break;
		}
	}

	return ret;
}

struct famfs_inode *
famfs_icache_find(struct famfs_icache *icache, uint64_t nodeid)
{
	struct famfs_inode *ret = NULL;

	pthread_mutex_lock(&icache->mutex);

	ret = famfs_icache_find_locked(icache, nodeid);

	pthread_mutex_unlock(&icache->mutex);
	return ret;
}

void
famfs_icache_insert_locked(
	struct famfs_icache *icache,
	struct famfs_inode *inode)
{
	/* TODO make this an insertion sort that aborts if the
	 * inode number is already present. That makes insertion
	 * "order n", but lookup is still order 1 because the nodeid is
	 * the address of the famfs_inode
	 */
	struct famfs_inode *prev, *next;

	assert(icache);
	assert(inode);

	/* for now: unconditional insert */
	prev = &icache->root;
	next = prev->next;
	next->prev = inode;
	inode->next = next;
	inode->prev = prev;
	prev->next = inode;

	icache->count++;
}

void
famfs_icache_unref_inode(
	struct famfs_icache *icache,
	struct famfs_inode *inode,
	void *owner,
	uint64_t n)
{
	if (!inode)
		return;

	pthread_mutex_lock(&icache->mutex);
	assert(inode->refcount >= n);
	assert(inode->owner == owner);
	inode->refcount -= n;
	if (!inode->refcount) {
		struct famfs_inode *prev, *next;
		uint64_t icache_count;

		prev = inode->prev;
		next = inode->next;
		next->prev = prev;
		prev->next = next;
		icache->count--;
		icache_count = icache->count;

		pthread_mutex_unlock(&icache->mutex);

		famfs_log(FAMFS_LOG_DEBUG,
			 "%s: icache_count=%ld i_ino=%d nodeid=%lx dropped\n",
			 __func__, icache_count, inode->ino, inode);

		close(inode->fd);
		if (inode->fmeta)
			free(inode->fmeta);
		free(inode);
	} else {
		uint64_t refcount = inode->refcount;
		uint64_t icache_count = icache->count;

		pthread_mutex_unlock(&icache->mutex);
		famfs_log(FAMFS_LOG_DEBUG,
			 "%s: icache_count=%ld i_ino=%d nodeid=%lx "
			 "deref=%ld newref=%ld\n",
			 __func__, icache_count, inode->ino, inode, n, refcount);

	}
}
