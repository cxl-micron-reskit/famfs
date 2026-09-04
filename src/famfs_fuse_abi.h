// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025-2026 Micron Technology, Inc.  All rights reserved.
 */

/*
 * famfs_fuse_abi.h - select the fuse kernel ABI famfs_fused is built for
 *
 * famfs_fused speaks one of two fuse DAX ABIs, decided at compile time by the
 * libfuse headers it is built against (the libfuse checkout in ./libfuse):
 *
 *  EXTMAP  - the extent-map ABI (Miklos Szeredi's libfuse "extent-map" work):
 *            FUSE_PASSTHROUGH_V2, daxdevs registered via
 *            FUSE_DEV_IOC_BACKING_CREATE, per-file maps pushed with
 *            FUSE_NOTIFY_BACKING_MAP (fuse_ext_map_populate()) at open time.
 *            Implemented in famfs_fused_extmap.c.
 *
 *  DAXFMAP - the famfs DAX_FMAP ABI (the famfs libfuse branches):
 *            FUSE_DAX_FMAP; the kernel pulls fmaps with FUSE_GET_FMAP and
 *            daxdev names with FUSE_GET_DAXDEV. Newer kernels of this ABI
 *            also accept daxdevs pushed via FUSE_DEV_IOC_DAXDEV_OPEN.
 *            Implemented in famfs_fused_daxfmap.c.
 *
 * The two libfuse variants are not link-compatible (different lowlevel ops,
 * fuse_file_info layout, exported functions), so this cannot be a runtime
 * choice. Both ABI files are always compiled; each is empty unless its ABI is
 * selected. famfs_fused.c itself has no ABI conditionals: it calls the
 * famfs_abi_*() hooks declared below.
 */
#ifndef FAMFS_FUSE_ABI_H
#define FAMFS_FUSE_ABI_H

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)
#endif
#include <fuse_lowlevel.h> /* also pulls in fuse_common.h and fuse_kernel.h */

#if defined(FUSE_EXT_MAP_CREATE) && defined(FUSE_CAP_PASSTHROUGH_V2)
#define FAMFS_FUSE_EXTMAP   1
#define FAMFS_FUSE_DAXFMAP  0
#define FAMFS_FUSE_ABI_NAME "extmap"
#elif defined(FUSE_CAP_DAX_FMAP)
#define FAMFS_FUSE_EXTMAP   0
#define FAMFS_FUSE_DAXFMAP  1
#define FAMFS_FUSE_ABI_NAME "daxfmap"
#else
#error "libfuse has neither famfs fuse ABI (need FUSE_EXT_MAP_CREATE or FUSE_CAP_DAX_FMAP)"
#endif

/*
 * Whether this build can push daxdevs to the kernel at lookup time. Always
 * for EXTMAP; for DAXFMAP only when libfuse has the DAXDEV_OPEN ioctl (the
 * famfs-6.14 branch does not; those kernels pull daxdevs via GET_DAXDEV).
 */
#if FAMFS_FUSE_EXTMAP || defined(FUSE_DEV_IOC_DAXDEV_OPEN)
#define FAMFS_FUSE_DAXDEV_PUSH 1
#else
#define FAMFS_FUSE_DAXDEV_PUSH 0
#endif

struct famfs_ctx;
struct famfs_inode;

/* INIT: negotiate the ABI's capability flags with the kernel */
void famfs_abi_init(struct famfs_ctx *lo, struct fuse_conn_info *conn);

/*
 * Register an open daxdev fd with the kernel as daxdev @devindex. The caller
 * closes @fd afterwards regardless of the result. Returns 0 on success, -1
 * on failure (logged).
 */
int famfs_abi_register_daxdev(fuse_req_t req, int fd, int devindex);

/* LOOKUP: ABI-specific fuse_entry_param bits (e.g. per-inode DAX) */
void famfs_abi_lookup_entry(struct fuse_entry_param *e);

/*
 * OPEN: ABI-specific open work (e.g. push the file's map and set the
 * passthrough ids in @fi). Returns 0 or an errno; on error the caller
 * replies with it and the open fails.
 */
int famfs_abi_open(fuse_req_t req, struct famfs_ctx *lo,
		   struct famfs_inode *inode, struct fuse_file_info *fi);

/* icache free_inode hook: release per-inode kernel state */
void famfs_abi_inode_free(void *owner, struct famfs_inode *inode);

/* Install ABI-specific lowlevel ops (e.g. get_fmap / get_daxdev) */
void famfs_abi_set_ops(struct fuse_lowlevel_ops *ops);

#endif /* FAMFS_FUSE_ABI_H */
