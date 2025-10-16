// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

#define _GNU_SOURCE
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include "famfs_fused_icache.h"
#include "famfs_fused.h"

int famfs_icache_init(
	void *owner,
	struct famfs_icache *icache,
	const char *shadow_root)
{
	memset(icache, 0, sizeof(*icache));
	pthread_mutex_init(&icache->mutex, NULL);
	pthread_mutex_init(&icache->flock_mutex, NULL);
	icache->owner = owner;
	
	/* Root inode setup */
	icache->root.next = icache->root.prev = &icache->root;
	icache->root.flags = FAMFS_ROOTDIR;
	icache->root.ftype = FAMFS_FDIR;
	icache->root.ino = FUSE_ROOT_ID;
	icache->root.name = strdup(".");
	icache->root.icache = icache;
	icache->root.refcount = 2;
	icache->root.fd = -1;

	if (shadow_root) {
		icache->root.fd = open(shadow_root, O_PATH);
		if (icache->root.fd == -1) {
			famfs_log(FAMFS_LOG_ERR, "open(\"%s\", O_PATH): %m\n",
				  shadow_root);
			return -1;
		}
		icache->shadow_root = strdup(shadow_root);
	}
	return 0;
}

void famfs_icache_destroy(struct famfs_icache *icache)
{
	while (icache->root.next != &icache->root) {
		struct famfs_inode* next = icache->root.next;

		icache->root.next = next->next;
		if (next && next != &icache->root)
			famfs_inode_free(next);
	}
	if (icache->shadow_root)
		free(icache->shadow_root);
}

void famfs_icache_flock(struct famfs_inode *inode)
{
	struct famfs_icache *icache = inode->icache;

	pthread_mutex_lock(&icache->flock_mutex);
	inode->flock_held = 1;
}

void famfs_icache_unflock(struct famfs_inode *inode)
{
	struct famfs_icache *icache = inode->icache;

	if (!inode->flock_held) {
		famfs_log(FAMFS_LOG_ERR, "%s: ino %d name %s flock not held\n",
			  __func__, inode->ino, inode->name);
		return;
	}
	inode->flock_held = 0;
	pthread_mutex_unlock(&icache->flock_mutex);
}

void dump_inode(const char *caller, struct famfs_inode *inode, int loglevel)
{
	famfs_log(loglevel,
		  "%s: %s ino=%ld nodeid=%llx flags=%d refcount=%ld ftype=%d "
		  "icache=%llx Parent=%llx pin=%d name=(%s)\n",
		  caller,
		  (inode->ftype == FAMFS_FREG) ? "FILE":"DIR",
		  inode->ino, (u64)inode, inode->flags,
		  inode->refcount, inode->ftype,
		  (u64)(inode->icache),
		  (inode->parent) ? inode->parent->ino : 0,
		  inode->pinned,
		  inode->name);
	if (inode->ftype == FAMFS_FDIR && inode->fmeta)
		famfs_log(FAMFS_LOG_ERR, "%s: dir inode has fmeta %p\n",
			  __func__, inode->fmeta);
}

void dump_icache(struct famfs_icache *icache, int loglevel)
{
	struct famfs_inode *p;
	size_t nino = 0;

	pthread_mutex_lock(&icache->mutex);
	famfs_log(loglevel, "%s: count=%ld\n",
	       __func__, icache->count);

	dump_inode(__func__, &icache->root, loglevel);
	for (p = icache->root.next; p != &icache->root; p = p->next) {
		dump_inode(__func__, p, loglevel);
		nino++;
	}
	pthread_mutex_unlock(&icache->mutex);
	famfs_log(loglevel, "   %ld inodes cached\n", nino);
}

/*
 * Move to new: famfs_icache.c
 */

struct famfs_inode *
famfs_inode_alloc(
	struct famfs_icache *icache,
	int fd,
	const char *name,
	ino_t inode_num,
	dev_t dev,
	struct famfs_log_file_meta *fmeta,
	struct stat *attrp,
	enum famfs_fuse_ftype ftype,
	struct famfs_inode *parent)
{
	struct famfs_inode *inode;

	inode = calloc(1, sizeof(*inode));
	if (!inode)
		return NULL;

	inode->icache = (void *)icache;
	inode->refcount = 1;

	inode->fd = fd;
	inode->ino = inode_num;
	inode->dev = dev;
	inode->fmeta = fmeta;
	inode->attr = *attrp;
	inode->ftype = ftype;
	inode->name = strdup(name);
	inode->parent = parent;

	/* Ref will be put on parent when the inode is inserted into icache */

	return inode;
}

/**
 * famfs_find_inode_locked(): find a cached famfs_inode
 *
 * Caller must hold the mutex
 */
struct famfs_inode *
famfs_icache_find_get_from_ino_locked(
	struct famfs_icache *icache, uint64_t ino)
{
	/* TODO: replace this lookup mechanism with Bernd's wbtree lookup code */
	struct famfs_inode *p;
	struct famfs_inode *inode = NULL;

	if (ino == 1) {
		inode = &icache->root;
		inode->refcount++;
		return inode;
	}

	icache->search_count++;
	for (p = icache->root.next; p != &icache->root; p = p->next) {
		/* Nodeid is the address of the entry we're looking for */
		icache->nodes_scanned++;
		if (p->ino == ino) {
			FAMFS_ASSERT(__func__, p->refcount > 0 || p->pinned);
			inode = p;
			inode->refcount++;
			break;
		}
	}
	if (!inode)
		icache->search_fail_ct++;

	return inode;
}

struct famfs_inode *
famfs_icache_find_get_from_ino(struct famfs_icache *icache, uint64_t ino)
{
	struct famfs_inode *ret = NULL;

	pthread_mutex_lock(&icache->mutex);

	ret = famfs_icache_find_get_from_ino_locked(icache, ino);

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

	FAMFS_ASSERT(__func__, icache);
	FAMFS_ASSERT(__func__, inode);

	/* When inserted, there is a base+1 refcount. Must call putref
	 * if you don't want to keep using it */
	inode->refcount = 2;

	/* for now: unconditional insert */
	prev = &icache->root;
	next = prev->next;
	next->prev = inode;
	inode->next = next;
	inode->prev = prev;
	prev->next = inode;

	inode->icache = icache;
	famfs_inode_getref_locked(inode->parent);

	icache->count++;
}

void
famfs_inode_free(struct famfs_inode *inode)
{
	if (inode->ino == FUSE_ROOT_ID)
		return;

	if (inode->fd > 0)
		close(inode->fd);
	if (inode->fmeta)
		free(inode->fmeta);
	if (inode->name)
		free(inode->name);
	free(inode);
}

void famfs_inode_putref_locked(struct famfs_inode *inode, uint64_t count)
{
	FAMFS_ASSERT(__func__, inode);
	FAMFS_ASSERT(__func__, inode->refcount >= count);
	inode->refcount -= count;

	if (!inode->refcount && !inode->pinned && inode->ino != FUSE_ROOT_ID) {
		struct famfs_inode *prev, *next;

		prev = inode->prev;
		next = inode->next;
		next->prev = prev;
		prev->next = next;
		inode->icache->count--;
		inode->icache = NULL;

		if (inode->parent)
			famfs_inode_putref_locked(inode->parent, 1);

		inode->parent = 0;
		famfs_inode_free(inode);
	}
};

void famfs_inode_putref(
	struct famfs_inode *inode)
{
	FAMFS_ASSERT(__func__, inode->icache);
	pthread_mutex_lock(&inode->icache->mutex);
	famfs_inode_putref_locked(inode, 1);
	pthread_mutex_unlock(&inode->icache->mutex);
}

void
famfs_icache_unref_inode(
	struct famfs_icache *icache,
	struct famfs_inode *inode,
	uint64_t n)
{
	if (!inode)
		return;

	pthread_mutex_lock(&icache->mutex);
	FAMFS_ASSERT(__func__, icache);
	FAMFS_ASSERT(__func__, inode);
	FAMFS_ASSERT(__func__, inode->refcount >= n);
	FAMFS_ASSERT(__func__, inode->icache == icache);

	famfs_inode_putref_locked(inode, n);

	pthread_mutex_unlock(&icache->mutex);
}

struct famfs_inode *
famfs_get_inode_from_nodeid_locked(
	struct famfs_icache *icache,
	fuse_ino_t nodeid)
{
	struct famfs_inode *inode;

	if (nodeid == FUSE_ROOT_ID)
		inode = &icache->root;
	else
		inode = (struct famfs_inode *)(uintptr_t)nodeid;

	if ((inode->icache != icache) || (inode->refcount < 1)) {
		inode = NULL;
		goto unlock_out;
	}
unlock_out:
	famfs_inode_getref_locked(inode);
	return inode;
}

/**
 * famfs_get_inode_from_nodeid()
 *
 * Find an inode and get a ref on it from its nodeid
 */
struct famfs_inode *
famfs_get_inode_from_nodeid(
	struct famfs_icache *icache,
	fuse_ino_t nodeid)
{

	struct famfs_inode *inode;

	/* XXX Note this applies the assumption that nodeid is the address
	 * of the famfs_inode. I don't think this is safe without verifying
	 * that it is *still* the address of the famfs_inode, which would
	 * require looking for it via the inode cache */

	pthread_mutex_lock(&icache->mutex);
	inode = famfs_get_inode_from_nodeid_locked(icache, nodeid);
	pthread_mutex_unlock(&icache->mutex);

	return inode;
}
