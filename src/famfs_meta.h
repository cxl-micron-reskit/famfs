/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * This file system, originally based on ramfs the dax support from xfs,
 * is intended to allow multiple host systems to mount a common file system
 * view of dax files that map to shared memory.
 */
#ifndef FAMFS_META_H
#define FAMFS_META_H

#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/famfs_ioctl.h>
#include <assert.h>

#include "famfs.h"

#define STATIC_ASSERT(cond, msg) typedef char static_assertion_##msg[(cond) ? 1 : -1]

#define FAMFS_SUPER_MAGIC      0x87b282ff
#define FAMFS_CURRENT_VERSION  47
//#define FAMFS_MAX_DAXDEVS      64

#define FAMFS_LOG_OFFSET    0x200000 /* 2MiB */
#define FAMFS_LOG_LEN       0x800000 /* 8MiB */

#define FAMFS_SUPERBLOCK_SIZE FAMFS_LOG_OFFSET
#define FAMFS_SUPERBLOCK_MAX_DAXDEVS 1

#define FAMFS_ALLOC_UNIT 0x200000 /* 2MiB allocation unit */

STATIC_ASSERT(!(FAMFS_LOG_LEN & (FAMFS_LOG_LEN - 1)), FAMFS_LOG_LEN_must_be_power_of_2);
STATIC_ASSERT(!(FAMFS_ALLOC_UNIT & (FAMFS_ALLOC_UNIT - 1)), FAMFS_ALLOC_UNIT_must_be_power_of_2);

static inline size_t round_size_to_alloc_unit(u64 size)
{
	return ((size + FAMFS_ALLOC_UNIT - 1) / FAMFS_ALLOC_UNIT) * FAMFS_ALLOC_UNIT;
}

#define FAMFS_DEVNAME_LEN 64

struct famfs_daxdev {
	size_t              dd_size;
	uuid_le             dd_uuid;
	/* TODO: what is an invariant way to reference a DAX device? */
	char                dd_daxdev[FAMFS_DEVNAME_LEN];
};

/* ts_sb_flags */
#define	FAMFS_PRIMARY_SB  (1 << 0) /* This device is the primary superblock of this famfs instance */


/* Lives at the base of a tagged tax device: */
struct famfs_superblock {
	u64                 ts_magic;
	u64                 ts_version;
	u64                 ts_log_offset;  /* offset to the start of the log file */
	u64                 ts_log_len;
	uuid_le             ts_uuid;        /* UUID of this file system */
	uuid_le             ts_dev_uuid;    /* uuid of this device */
	uuid_le             ts_system_uuid; /* system uuid */
	u64                 ts_crc;         /* Covers all fields prior to this one */
	u32                 ts_sb_flags;
	struct famfs_daxdev ts_daxdev;
};

enum famfs_system_role {
	FAMFS_MASTER=1,/* System is the master, and can write the log */
	FAMFS_CLIENT,  /* System is a client - cannot write the log or sb */
	FAMFS_NOSUPER, /* No valid superblock, so role is ambiguous */
};

/* Extent types */

enum famfs_log_ext_type {
	FAMFS_EXT_SIMPLE,
	FAMFS_EXT_INTERLEAVE,
};

/* Maximum number of extents in a FC extent list */
#define FAMFS_MAX_SIMPLE_EXTENTS 8
#define FAMFS_MAX_INTERLEAVED_EXTENTS 1

/* TODO: get rid of this extent type, and use the one from the kernel instead
 * (which will avoid silly translations...
 */
struct famfs_simple_extent {
	/* This extent is on the dax device with the superblock */
	u64 se_devindex; /* Must be 0 until multi-device support appears */
	u64 se_offset;
	u64 se_len;
};

/**
 * @famfs_interleaved_ext
 *
 * We consider this a single file mapping "extent", but it has "sub-extents"
 * to describe the ranges that back each strip.
 * In the future when each strip might be on a different memory(dax) device,
 * so ie_strips will need to be a struct that contains a device index.
 *
 * For now we require that the entire map be a single famfs_interleaved_ext
 * (i.e. FAMFS_MAX_INTERLEAVED_EXTENTS == 1)
 */
struct famfs_interleaved_ext {
	u64 ie_nstrips;
	u64 ie_chunk_size;
	struct famfs_simple_extent ie_strips[FAMFS_MAX_SIMPLE_EXTENTS];
};

/*
 * The map of a file's data in the log.
 * It can either be a simple extent list, or an interleaved_ext list
 * (and the latter only allows one interleaved extent).
 *
 * Note these are arrays and not pointers to make this structure friendly to
 * writing to and reading from the metadata log. This may evolve if we adopt a
 * proper omf (on-media format) implementation.
 */
struct famfs_log_fmap {
	u32 fmap_ext_type; /* enum famfs_log_ext_type */
	u32 fmap_nextents;
	union {
		struct famfs_simple_extent se[FAMFS_MAX_SIMPLE_EXTENTS];
		struct famfs_interleaved_ext ie[FAMFS_MAX_INTERLEAVED_EXTENTS];
		/* will include the other extent types eventually */
	};
};

enum famfs_log_entry_type {
	FAMFS_LOG_FILE,    /* This type of log entry creates a file */
	FAMFS_LOG_MKDIR,
	FAMFS_LOG_DELETE,
	FAMFS_LOG_INVALID,
};

#define FAMFS_MAX_PATHLEN 80
#define FAMFS_MAX_HOSTNAME_LEN 32
#define FAMFS_FM_BUF_LEN 512

/* fm_flags */
#define FAMFS_FM_ALL_HOSTS_RO (1 << 0)
#define FAMFS_FM_ALL_HOSTS_RW (1 << 1)


/* This log entry creates a directory */
struct famfs_mkdir {
	uid_t   md_uid;
	gid_t   md_gid;
	mode_t  md_mode;

	u8      md_relpath[FAMFS_MAX_PATHLEN];
};

/* This log entry creates a file */
struct famfs_file_meta {
	u64     fm_size;
	u32     fm_flags;

	uid_t   fm_uid;
	gid_t   fm_gid;
	mode_t  fm_mode;

	u8      fm_relpath[FAMFS_MAX_PATHLEN];
	u8      ext_type;
	struct  famfs_log_fmap fm_fmap;
};

struct famfs_log_entry {
	u64     famfs_log_entry_seqnum;
	u32     famfs_log_entry_type; /* FAMFS_LOG_FILE_CREATION or FAMFS_LOG_ACCESS */
	union {
		struct famfs_file_meta     famfs_fm;
		struct famfs_mkdir         famfs_md;
	};
	unsigned long famfs_log_entry_crc;
};

#define FAMFS_LOG_MAGIC 0xbadcafef00d

/**
 * @famfs_log - the structure of the famfs log
 *
 * @famfs_log_magic: magic number
 * @famfs_log_len: total size of the log, including header and all valid entries
 * @famfs_log_last_index:  The last valid index (i.e. inclusive)
 * @famfs_log_crc: crc which covers the preceeding fields, which don't change
 * @famfs_log_next_seqnum: sequence number for the next log entry
 * @famfs_log_next_index: Index of the next (not yet inserted) log entry
 * @entries: Array of log entries. sizeof famfs_log, including all entries, must be
 *           <= @famfs_log_len
 */
struct famfs_log {
	u64     famfs_log_magic;
	u64     famfs_log_len;
	u64     famfs_log_last_index;
	unsigned long famfs_log_crc;
	u64     famfs_log_next_seqnum;
	u64     famfs_log_next_index;
	struct famfs_log_entry entries[];
};

static inline s64
log_slots_available(struct famfs_log *logp)
{
	s64 navail = logp->famfs_log_last_index - logp->famfs_log_next_index + 1;
	assert(navail >= 0);
	return navail;
}

#endif /* FAMFS__META_H */
