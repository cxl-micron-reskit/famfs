// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025-2026 Micron Technology, Inc.  All rights reserved.
 *
 * famfs_fused_extmap.c - the extent-map fuse ABI for famfs_fused
 * (FAMFS_FUSE_EXTMAP; see famfs_fuse_abi.h). Derived from Miklos Szeredi's
 * "famfs: use fuse extent map support" patch.
 *
 * Daxdevs are registered with the kernel as passthrough-v2 backing devices
 * (backing id = daxdev index + 1), and a file's extent map is pushed with
 * FUSE_NOTIFY_BACKING_MAP at first open, keyed by a per-inode backing id (the
 * famfs_inode address). The map is removed when the inode is freed.
 */
#define _GNU_SOURCE
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>

#include "famfs_lib.h"
#include "fuse_kernel.h" /* struct fuse_extent / fuse_daxdev_out */
#include "famfs_fused.h"
#include "famfs_fused_icache.h"
#include "famfs_fuse_abi.h"

#if FAMFS_FUSE_EXTMAP

void
famfs_abi_init(struct famfs_ctx *lo, struct fuse_conn_info *conn)
{
	(void)lo;

	if (!(conn->capable_ext & FUSE_CAP_PASSTHROUGH_V2)) {
		famfs_log(FAMFS_LOG_ERR,
			  "%s: Kernel not passthrough-capable\n", __func__);
		exit(1);
	}
	fuse_set_feature_flag(conn, FUSE_CAP_PASSTHROUGH_V2);
}

int
famfs_abi_register_daxdev(fuse_req_t req, int fd, int devindex)
{
	int rc;

	/* backing id 0 is reserved: daxdev N is backing id N + 1 */
	rc = fuse_passthrough_v2_open(fuse_req_session(req), fd,
				      devindex + 1, 0);
	if (rc < 0) {
		famfs_log(FAMFS_LOG_ERR,
			  "%s: fuse_passthrough_v2_open(index=%d) failed (rc=%d)\n",
			  __func__, devindex, rc);
		return -1;
	}
	return 0;
}

void
famfs_abi_lookup_entry(struct fuse_entry_param *e)
{
	e->dax = true;
}

static ssize_t
famfs_meta_to_fuse_msg(
	struct fuse_extent *exts,
	uint max_exts,
	const struct famfs_log_file_meta *fmeta,
	unsigned int *flags)
{
	const struct famfs_log_fmap *log_fmap = &fmeta->fm_fmap;
	const struct famfs_simple_extent *fse;
	u64 chunk_size = 0; /* nonzero: cyclic map, one chunk per strip */
	uint nextents;
	uint i;
	loff_t curr_off;

	/*
	 * A simple fmap maps one extent per on-media extent. An interleaved
	 * (striped) fmap maps as a cyclic map: a uniform list of one extent
	 * per strip that repeats. The kernel resolves file offset o with
	 * len = extent length and n = extent count as cycle k = (o / len) / n,
	 * extent s = (o / len) % n, backing address addr[s] + k * len. So each
	 * strip contributes exactly one chunk-sized extent at the strip's
	 * start; cycle k of strip s then lands at se_offset + k * chunk_size,
	 * which is the famfs stripe layout. (Emitting the whole strip length
	 * would make the chunk the strip and concatenate the strips.) The
	 * on-media log keeps the compact interleaved form.
	 */
	switch (log_fmap->fmap_ext_type) {
	case FAMFS_EXT_SIMPLE:
		nextents = log_fmap->fmap_nextents;
		fse = log_fmap->se;
		break;
	case FAMFS_EXT_INTERLEAVE: {
		const struct famfs_interleaved_ext *ie = &log_fmap->ie[0];

		/* Only a single interleaved extent is supported today */
		if (log_fmap->fmap_niext != 1 ||
		    ie->ie_nstrips == 0 || ie->ie_chunk_size == 0)
			return -EINVAL;

		nextents = ie->ie_nstrips;
		fse = ie->ie_strips;
		chunk_size = ie->ie_chunk_size;
		*flags |= FUSE_EXT_MAP_CYCLIC;
		break;
	}
	default:
		return -EINVAL;
	}

	if (nextents > max_exts)
		return -EOVERFLOW;

	curr_off = 0;
	for (i = 0; i < nextents; i++) {
		u64 len = chunk_size ? chunk_size : fse[i].se_len;

		/* A strip must hold at least one chunk */
		if (chunk_size && fse[i].se_len < chunk_size)
			return -EINVAL;

		exts[i].offset = curr_off;
		exts[i].length = len;
		exts[i].backing_id = fse[i].se_devindex + 1;
		exts[i].addr = fse[i].se_offset;
		curr_off += len;
	}

	return nextents;
}

static int famfs_push_map(
	fuse_req_t req,
	fuse_ino_t nodeid)
{
	struct famfs_ctx *lo = fuse_req_userdata(req);
	struct famfs_inode *inode = NULL;
	size_t max_exts = 1024;
	struct fuse_extent *exts;
	ssize_t num_exts;
	int err = 0;
	unsigned int flags = FUSE_EXT_MAP_CREATE;

	exts = calloc(max_exts, sizeof(struct fuse_extent));
	if (!exts) {
		err = ENOMEM;
		goto out_err;
	}

	/* The nodeid is the address of the famfs_inode. Retrieving it
	 * this way validates that there is indeed an inode at that address.
	 */
	inode = famfs_get_inode_from_nodeid(&lo->icache, nodeid);

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
	num_exts = famfs_meta_to_fuse_msg(exts, max_exts, inode->fmeta, &flags);
	if (num_exts <= 0) {
		/* Send reply without fmap */
		famfs_log(FAMFS_LOG_ERR,
			  "%s: %ld error putting fmap in message\n",
			 __func__, num_exts);
		err = EINVAL;
		goto out_err;
	}

	err = fuse_ext_map_populate(fuse_req_session(req), nodeid, num_exts, exts, flags);
	if (err)
		famfs_log(FAMFS_LOG_ERR, "%s: fuse_fixmap_create returned err %d\n",
			 __func__, err);
out_err:
	if (inode)
		famfs_inode_putref(inode);

	free(exts);

	return err;
}

int
famfs_abi_open(
	fuse_req_t req,
	struct famfs_ctx *lo,
	struct famfs_inode *inode,
	struct fuse_file_info *fi)
{
	uint64_t backing_id = (uintptr_t) inode;
	int err = 0;

	pthread_mutex_lock(&lo->daxdev_push_mutex);
	if (!inode->mapping_pushed) {
		err = famfs_push_map(req, backing_id);
		if (!err)
			inode->mapping_pushed = true;
	}
	pthread_mutex_unlock(&lo->daxdev_push_mutex);
	if (err)
		return err;

	fi->backing_id = 1; /* Enable passthrough, v2 uses the 64 bit ID */
	fi->backing_id_64 = backing_id;
	return 0;
}

void
famfs_abi_inode_free(
	void *owner,
	struct famfs_inode *inode)
{
	struct famfs_ctx *lo = owner;

	if (inode->mapping_pushed)
		fuse_passthrough_v2_close(lo->se, (uintptr_t) inode);
}

void
famfs_abi_set_ops(struct fuse_lowlevel_ops *ops)
{
	(void)ops; /* maps are pushed; the kernel never asks for them */
}

#endif /* FAMFS_FUSE_EXTMAP */
