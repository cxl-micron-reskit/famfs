/*
 * Copyright (C) 2015-2016 Micron Technology, Inc.  All rights reserved.
 *
 * mcache subsystem for caching mblocks from the mpool subsystem
 * based on ramfs from Linux
 */
/* internal.h: ramfs internal definitions
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef MCACHE_INTERNAL_H
#define MCACHE_INTERNAL_H

#include <linux/parser.h>

#include <mpcore/init.h>
#include <mpcore/mblock.h>
#include <mpcore/mpcore_defs.h>

/*
 * TODO: This belongs in include/uapi/linux/magic.h.
 */
#define MCACHE_SUPER_MAGIC      0xceedee

/*
 * struct mcache_map_meta
 *
 * Each mcache map file has this hanging from its inode->i_private.
 */
struct mcache_map_meta {
	size_t                      mcm_bktsz;
	size_t                      mcm_mbdescc;
	struct mblock_descriptor   *mcm_mbdescv[];  /* flexible array */
};

struct mcache_mount_opts {
	umode_t mmo_mode;
	uint    mmo_minor;
	int     mmo_force;
};

struct mcache_fs_info {
	struct mcache_mount_opts    fsi_mntopts;
	struct mpool_descriptor    *fsi_mpdesc;
	u64                         fsi_dsid;
	struct mpc_unit            *fsi_unit;
};

extern const struct inode_operations mcache_file_inode_operations;
extern const struct file_operations mcache_file_operations;

/**
 * mcache_map_meta_free() - Free mcache meta data
 * @map:
 *
 */
void
mcache_map_meta_free(
	struct mcache_map_meta *map);

/**
 * mcache_init() - Called by mpc_load() at module load
 * @sb:
 */
int
mcache_init(void);

/**
 * mcache_exit() - Called by mpc_unload() at module unload
 * @sb:
 */
void
mcache_exit(void);

#endif /* MCACHE_INTERNAL_H */
