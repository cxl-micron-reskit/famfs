// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025-2026 Micron Technology, Inc.  All rights reserved.
 *
 * famfs_fused_daxfmap.c - the famfs DAX_FMAP fuse ABI for famfs_fused
 * (FAMFS_FUSE_DAXFMAP; see famfs_fuse_abi.h).
 *
 * The kernel pulls a file's fmap with FUSE_GET_FMAP (served from the famfs
 * fmap wire format, famfs_fmap.h) and daxdev names with FUSE_GET_DAXDEV.
 * Kernels of this ABI that have FUSE_DEV_IOC_DAXDEV_OPEN take daxdevs pushed
 * at lookup time instead, and are simple-extent-only (interleaved fmaps are
 * unrolled for them).
 */
#define _GNU_SOURCE
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include "famfs_lib.h"
#include "fuse_kernel.h" /* struct fuse_extent / fuse_daxdev_out */
#include "famfs_fmap.h"
#include "famfs_fused.h"
#include "famfs_fused_icache.h"
#include "famfs_fuse_abi.h"

#if FAMFS_FUSE_DAXFMAP

#define FMAP_MSG_MAX 4096
/* Upper clamp on the kernel-provided GET_FMAP reply-buffer size (defensive). */
#define FMAP_REPLY_MAX (16 * 1024 * 1024)

void
famfs_abi_init(struct famfs_ctx *lo, struct fuse_conn_info *conn)
{
	if (conn->capable & FUSE_CAP_PASSTHROUGH)
		famfs_log(FAMFS_LOG_NOTICE, "%s: Kernel is passthrough-capable\n",
			 __func__);

	if (conn->capable_ext & FUSE_CAP_DAX_FMAP) {
		famfs_log(FAMFS_LOG_NOTICE,  "%s: Kernel is DAX_IOMAP-capable\n",
			 __func__);
		if (lo->daxdev) {
			famfs_log(FAMFS_LOG_NOTICE,
				 "%s: ENABLING DAX_IOMAP\n", __func__);
			conn->want_ext |= FUSE_CAP_DAX_FMAP;
		} else {
			famfs_log(FAMFS_LOG_NOTICE,
				 "%s: disabling DAX_IOMAP (no daxdev)\n",
				 __func__);
		}
	}
}

int
famfs_abi_register_daxdev(fuse_req_t req, int fd, int devindex)
{
#ifdef FUSE_DEV_IOC_DAXDEV_OPEN
	int rc;

	rc = fuse_daxdev_open(req, fd, devindex);
	if (rc <= 0) {
		famfs_log(FAMFS_LOG_ERR,
			  "%s: fuse_daxdev_open(index=%d) failed (rc=%d)\n",
			  __func__, devindex, rc);
		return -1;
	}
	return 0;
#else
	(void)req;
	(void)fd;
	famfs_log(FAMFS_LOG_ERR,
		  "%s: this libfuse cannot push daxdevs (index=%d)\n",
		  __func__, devindex);
	return -1;
#endif
}

void
famfs_abi_lookup_entry(struct fuse_entry_param *e)
{
	(void)e;
}

int
famfs_abi_open(
	fuse_req_t req,
	struct famfs_ctx *lo,
	struct famfs_inode *inode,
	struct fuse_file_info *fi)
{
	(void)req;
	(void)lo;
	(void)inode;
	(void)fi;
	return 0;
}

void
famfs_abi_inode_free(
	void *owner,
	struct famfs_inode *inode)
{
	(void)owner;
	(void)inode;
}

static void
famfs_get_fmap(
	fuse_req_t req,
	fuse_ino_t nodeid,
	size_t size)
{
	struct famfs_ctx *lo = fuse_req_userdata(req);
	ssize_t fmap_bufsize;
	struct famfs_inode *inode = NULL;
	char *fmap_message = NULL;
	ssize_t fmap_size;
	int simple_fmap;
	int err = 0;

	/*
	 * The kernel passes the size of its GET_FMAP reply buffer via the
	 * request's fuse_getxattr_in.size, so a large fmap can be returned in
	 * one shot. Clamp defensively: an older kernel that does not send the
	 * size leaves it unspecified, so fall back to the minimum and cap
	 * absurd values.
	 */
	fmap_bufsize = size;
	if (fmap_bufsize < FMAP_MSG_MAX)
		fmap_bufsize = FMAP_MSG_MAX;
	if (fmap_bufsize > FMAP_REPLY_MAX)
		fmap_bufsize = FMAP_REPLY_MAX;

	fmap_message = calloc(1, fmap_bufsize);
	if (!fmap_message) {
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

	/*
	 * Choose the wire fmap form. Unroll to a simple-extent list only when
	 * the kernel is simple-only: either forced (-o simple_fmaps) or detected
	 * via daxdev-push mode - a daxdev was successfully registered through
	 * FUSE_DEV_IOC_DAXDEV_OPEN, which is the new (simple-only) ABI. The push
	 * happens at lookup, before any GET_FMAP, so this is settled by now.
	 * Otherwise emit the compact interleaved form a legacy kernel expects.
	 */
	simple_fmap = lo->simple_fmaps || (lo->daxdev_max_pushed >= 0);

	/* XXX: FAMFS_FMAP_WIRE_FILE_REG - mark sb and log correctly */
	fmap_size = famfs_log_file_meta_to_msg(fmap_message, fmap_bufsize,
					       FAMFS_FMAP_WIRE_FILE_REG,
					       inode->fmeta, simple_fmap);
	if (fmap_size <= 0) {
		/* Send reply without fmap */
		famfs_log(FAMFS_LOG_ERR,
			  "%s: %ld error putting fmap in message\n",
			 __func__, fmap_size);
		err = EINVAL;
		goto out_err;
	}

	err = fuse_reply_buf(req, fmap_message, fmap_size /* FMAP_MSG_MAX */);
	if (err)
		famfs_log(FAMFS_LOG_ERR, "%s: fuse_reply_buf returned err %d\n",
			 __func__, err);

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
	struct famfs_ctx *fd = fuse_req_userdata(req);
	struct fuse_daxdev_out daxdev;
	int err = 0;

	famfs_log(FAMFS_LOG_NOTICE, "%s: daxdev_index=%d\n",
		 __func__, daxdev_index);
	memset(&daxdev, 0, sizeof(daxdev));

	/* Fill in daxdev struct */
	if (daxdev_index != 0) {
		/* XXX drop this test when we support more than one daxdev */
		famfs_log(FAMFS_LOG_ERR, "%s: non-zero daxdev index\n",
			  __func__);
		err = EINVAL;
		goto out_err;
	}
	if (!fd->daxdev) {
		famfs_log(FAMFS_LOG_ERR, "%s: dax not enabled\n", __func__);
		err = EOPNOTSUPP;
		goto out_err;
	}

	strncpy(daxdev.name, fd->daxdev_table[daxdev_index].dd_daxdev,
		FAMFS_DEVNAME_LEN - 1);

	err = fuse_reply_buf(req, (void *)&daxdev, sizeof(daxdev));
	if (err)
		famfs_log(FAMFS_LOG_ERR,
			  "%s: fuse_reply_buf returned err %d\n",
			 __func__, err);
	return;

out_err:
	fuse_reply_err(req, err);
}

void
famfs_abi_set_ops(struct fuse_lowlevel_ops *ops)
{
	ops->get_fmap   = famfs_get_fmap;
	ops->get_daxdev = famfs_get_daxdev;
}

#endif /* FAMFS_FUSE_DAXFMAP */
