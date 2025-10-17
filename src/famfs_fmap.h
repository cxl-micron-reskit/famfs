// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */

#ifndef FAMFS_FMAP_H
#define FAMFS_FMAP_H

#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/famfs_ioctl.h>
#include <assert.h>
#include "famfs.h"

#define FAMFS_LOG_VERSION 1

#define LOG_HEADER_TAG     0xf00d
#define LOG_SIMPLE_EXT_TAG 0xbeef
#define LOG_IEXT_TAG       0xcafe

//#define FAMFS_MAX_STRIPS 64
#define FAMFS_MAX_SIMPLE_EXT 16 /* I don't see a reason to make this big...? */

/* Structs used serializing fmaps into buffers/message */
struct fmap_simple_ext {
	u16 struct_tag;
	u16 se_devindex; /* Must be 0 until multi-device support appears */
	u32 reserved;
	u64 se_offset;
	u64 se_len;
};

struct fmap_log_iext {
	u16 struct_tag;
	u16 ie_nstrips;
	u32 reserved;
	u64 ie_chunk_size;
	u64 ie_nbytes;
	/* What will follow is ie_nstrips x famfs_buf_simple_ext */
};

/* This is for in-memory use; a fmap_log_iext plus a pointer to the strip extents */
struct fmap_mem_iext {
	struct fmap_log_iext iext;
	struct fmap_simple_ext *se;
};

struct fmap_log_header {
	u16 struct_tag;
	u8  fmap_log_version; /* FAMFS_BUF_VERSION */
	u8  fmap_ext_type;
	u16 reserved;
	union {
		u16 next; /* number of simple extents */
		u16 niext; /* number of interleaved extents */
	};
	u64 reserved2;
	/* What will follow in the msg/buffer is either:
	 * * next x famfs_buf_simple_ext
	 * * niext x famfs_buf_iext
	 *
	 * (Note: if there is more than one famfs_buf_iext, they may each be
	 * different sizes)
	 */
};

struct fmap_mem_header {
	struct fmap_log_header flh;
	union {
		struct fmap_mem_iext *ie;
		struct fmap_simple_ext *se;
	};
};

struct fmap_mem_header *alloc_simple_fmap(int next);
struct fmap_mem_header *alloc_interleaved_fmap(int ninterleave,
		int nstrips_per_interleave, int verbose);
void free_mem_fmap(struct fmap_mem_header *fm);
int validate_mem_fmap(struct fmap_mem_header *fm, int enforce, int verbose);

ssize_t famfs_log_file_meta_to_msg(char *msg, uint msg_size, int file_type,
		const struct famfs_log_file_meta *fmeta);

#endif /* FAMFS_FMAP_H */
