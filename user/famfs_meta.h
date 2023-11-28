/* SPDX-License-Identifier: GPL-2.0 */
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023 Micron Technology, Inc.
 *
 * This file system, originally based on ramfs the dax support from xfs,
 * is intended to allow multiple host systems to mount a common file system
 * view of dax files that map to shared memory.
 */
#ifndef FAMFS_META_H
#define FAMFS_META_H

#include <linux/types.h>
#include <linux/uuid.h>

#include "famfs.h"
#include "famfs_ioctl.h"

/**
 * Allocation metadata: superblock and log
 *
 * Superblock exposed through superblock file (.meta/superblock)
 * Log and log_cb exposed via log file (.meta/log)
 *
 * Filesystem quirks
 *
 * Famfs is a DAX file system based on RAMFS. Files are backed by DAX memory, but (like ramfs)
 * inodes are not persisted to media. This makes sense because famfs is largely aimed at
 * non-persistent dax devices.
 *
 * Famfs Structure
 *
 * A famfs file system has a root dax device. The superblock (struct famfs superblock) lives
 * at offset 0 on the root dax device. The superblock is exposed as a file at .meta/sb via the
 * FAMFSIOC_MAP_SUPERBLOCK ioctl. *fixme*
 *
 * The root log is located in the root dax device, at sb->ts_log_offset. The log length
 * is sb->ts_log_len. The root log is exposed as the file .meta/rootlog via the
 * FAMFSIOC_MAP_ROOTLOG ioctl.
 *
 * Mounting is somewhat non-standard. After issuing the mount command, there is an empty famfs
 * file system. The filesystem is not fully usable until the userspace utility does the following:
 *
 * * Write the 2MiB superblock to offset 0 on the root dax device
 * * Create the superblock file (FAMFSIOC_MAP_SUPERBLOCK)
 * * Create the rootlog file (FAMFSIOC_MAP_ROOTLOG)
 * * Replay the rootlog (and any other logs) into the filesystem, which populates the files
 *   which are mapped to dax memory (likely cxl fam)
 *
 * TODO: the stuff documented above is not implemented yet...
 */

#define FAMFS_SUPER_MAGIC      0x09211963
#define FAMFS_CURRENT_VERSION  42
#define FAMFS_MAX_DAXDEVS      64

#define FAMFS_LOG_OFFSET    0x200000 /* 2MiB */
#define FAMFS_LOG_LEN       0x800000 /* 8MiB */

#define FAMFS_SUPERBLOCK_SIZE FAMFS_LOG_OFFSET
#define FAMFS_SUPERBLOCK_MAX_DAXDEVS 1

#define FAMFS_ALLOC_UNIT 0x200000 /* 2MiB allocation unit */

static inline size_t round_size_to_alloc_unit(u64 size)
{
	return ((size + FAMFS_ALLOC_UNIT - 1) / FAMFS_ALLOC_UNIT) * FAMFS_ALLOC_UNIT;
}

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
	uuid_le             ts_uuid;
	u64                 ts_crc;         /* Covers all fields prior to this one */
	u32                 ts_num_daxdevs; /* limit is FAMFS_MAX_DAXDEVS */
	u32                 ts_sb_flags;
	struct famfs_daxdev ts_devlist[FAMFS_SUPERBLOCK_MAX_DAXDEVS];
};

/* Lives at the base of the .meta/log file: */
struct famfs_log_cb {
	u64 num_log_entries;
	u64 next_free_offset; /* Offset where next log entry will go */
};


/* Extent types */

enum famfs_log_ext_type {
	FAMFS_EXT_SIMPLE,
};

struct famfs_simple_extent {
	/* Tihs extent is on the dax device with the superblock */
	u64 famfs_extent_offset;
	u64 famfs_extent_len;
};

struct famfs_log_extent {
	u32 famfs_extent_type;
	union {
		struct famfs_simple_extent se;
		/* will include the other extent types eventually */
	};
};

enum famfs_log_entry_type {
	FAMFS_LOG_FILE,    /* This type of log entry creates a file */
	FAMFS_LOG_MKDIR,
	FAMFS_LOG_ACCESS,  /* This type of log entry gives a host access to a file */
};

#define FAMFS_MAX_PATHLEN 80
#define FAMFS_MAX_HOSTNAME_LEN 32

/* famfs_fc_flags */
#define FAMFS_FC_ALL_HOSTS_RO (1 << 0)
#define FAMFS_FC_ALL_HOSTS_RW (1 << 1)

/* Maximum number of extents in a FC extent list */
#define FAMFS_FC_MAX_EXTENTS 8

/* This log entry creates a directory */
struct famfs_mkdir {
	/* TODO: consistent field naming */
	uid_t   fc_uid;
	gid_t   fc_gid;
	mode_t  fc_mode;

	u8      famfs_relpath[FAMFS_MAX_PATHLEN];
};

/* This log entry creates a file */
struct famfs_file_creation {
	/* TODO: consistent field naming */
	u64     famfs_fc_size;
	u32     famfs_nextents;
	u32     famfs_fc_flags;

	uid_t   fc_uid;
	gid_t   fc_gid;
	mode_t  fc_mode;

	u8      famfs_relpath[FAMFS_MAX_PATHLEN];
	struct  famfs_log_extent famfs_ext_list[FAMFS_FC_MAX_EXTENTS];
};

/* A log entry of type FAMFS_LOG_ACCESS contains a struct famfs_file_access entry.
 */
struct famfs_file_access {
	char    fa_hostname[FAMFS_MAX_HOSTNAME_LEN];
	uid_t   fa_uid;
	gid_t   fa_gid;
	u8      fa_owner_perm;
	u8      fa_group_perm;
	u8      fa_other_perm;
};

struct famfs_log_entry {
	u64     famfs_log_entry_seqnum;
	u32     famfs_log_entry_type; /* FAMFS_LOG_FILE_CREATION or FAMFS_LOG_ACCESS */
	union {
		struct famfs_file_creation famfs_fc;
		struct famfs_mkdir         famfs_md;
		struct famfs_file_access   famfs_fa;
	};
};

#define FAMFS_LOG_MAGIC 0xbadcafef00d

struct famfs_log {
	u64     famfs_log_magic;
	u64     famfs_log_len;
	u64     famfs_log_next_seqnum;
	u64     famfs_log_next_index;
	u64     famfs_log_last_index; /* Log would overflow if we write past here */
	struct famfs_log_entry entries[];
};

#endif /* FAMFS__META_H */
