#ifndef TAGFS_META_H
#define TAGFS_META_H

#include <linux/types.h>
#include <linux/uuid.h>
//#include <linux/dax.h>

#include "tagfs.h"
#include "tagfs_ioctl.h"

/**
 * Allocation metadata: superblock and log
 *
 * Superblock exposed through superblock file (.meta/superblock)
 * Log and log_cb exposed via log file (.meta/log)
 *
 * Filesystem quirks
 *
 * Tagfs is a DAX file system based on RAMFS. Files are backed by DAX memory, but (like ramfs)
 * inodes are not persisted to media. This makes sense because tagfs is largely aimed at
 * non-persistent dax devices.
 *
 * Tagfs Structure
 *
 * A tagfs file system has a root dax device. The superblock (struct tagfs superblock) lives
 * at offset 0 on the root dax device. The superblock is exposed as a file at .meta/sb via the
 * TAGFSIOC_MAP_SUPERBLOCK ioctl.
 *
 * The root log is located in the root dax device, at sb->ts_log_offset. The initial log length
 * is sb->ts_log_len. The root log is exposed as the file .meta/rootlog via the
 * TAGFSIOC_MAP_ROOTLOG ioctl.
 *
 * Mounting is somewhat non-standard. After issuing the mount command, there is an empty tagfs
 * file system. The filesystem is not fully usable until the userspace utility does the following:
 *
 * * Write the 2MiB superblock to offset 0 on the root dax device
 * * Create the superblock file (TAGFSIOC_MAP_SUPERBLOCK)
 * * Create the rootlog file (TAGFSIOC_MAP_ROOTLOG)
 * * Replay the rootlog (and any other logs) into the filesystem, which populates the files
 *   which are mapped to dax memory (likely cxl fam)
 *
 * TODO: the stuff documented above is not implemented yet...
 */

#define TAGFS_SUPER_MAGIC      0x09211963
#define TAGFS_CURRENT_VERSION  42
#define TAGFS_MAX_DAXDEVS      64

#define TAGFS_LOG_OFFSET    0x200000 /* 2MiB */
#define TAGFS_LOG_LEN       0x800000 /* 8MiB */

#define TAGFS_SUPERBLOCK_SIZE TAGFS_LOG_OFFSET
#define TAGFS_SUPERBLOCK_MAX_DAXDEVS 1

#define TAGFS_ALLOC_UNIT 0x200000 /* 2MiB allocation unit */

static inline size_t round_size_to_alloc_unit(u64 size)
{
	return ((size + TAGFS_ALLOC_UNIT - 1) / TAGFS_ALLOC_UNIT) * TAGFS_ALLOC_UNIT;
}

struct tagfs_daxdev {
	size_t              dd_size;
//	struct dax_device  *dd_dax_device;
//	void               *dd_dax_device;
	uuid_le             dd_uuid;
	/* TODO: what is an invariant way to reference a DAX device? */
	char                dd_daxdev[TAGFS_DEVNAME_LEN];
};

/* ts_sb_flags */
#define	TAGFS_PRIMARY_SB  (1 << 0) /* This device is the primary superblock of this tagfs instance */


/* Lives at the base of a tagged tax device: */
struct tagfs_superblock {
	u64                 ts_magic;
	u64                 ts_version;
	u64                 ts_log_offset;  /* offset to the start of the log file */
	u64                 ts_log_len;
	uuid_le             ts_uuid;
	u64                 ts_crc;         /* Coves all fields prior to this one */
	u32                 ts_num_daxdevs; /* limit is TAGFS_MAX_DAXDEVS */
	u32                 ts_sb_flags;
	struct tagfs_daxdev ts_devlist[TAGFS_SUPERBLOCK_MAX_DAXDEVS];
};

/* Lives at the base of the .meta/log file: */
struct tagfs_log_cb {
	u64 num_log_entries;
	u64 next_free_offset; /* Offset where next log entry will go */
};


/* Extent types */

enum tagfs_log_ext_type {
  TAGFS_EXT_SIMPLE,
};

struct tagfs_simple_extent {
	/* Tihs extent is on the dax device with the superblock */
	u64 tagfs_extent_offset;
	u64 tagfs_extent_len;
};

struct tagfs_spanning_extent {
	/* This extent may span dax devices (tagged capacity instances) and
	 * therefore each extent must include a dax device uuid */
	/* TODO  */
};

struct tagfs_stripe_extent {
	/* This extent will specfy an ordered set of dax devices, a chunk size,
	 * and a length (which multiple of (ndevices * chunksize) so there is
	 * an integer number of full stripes */
	/* TODO  */
};

struct tagfs_log_extent {
	u32 tagfs_extent_type;
	union {
		struct tagfs_simple_extent se;
		/* will include the other extent types eventually */
	};
};

enum tagfs_log_entry_type {
	TAGFS_LOG_FILE,    /* This type of log entry creates a file */
	TAGFS_LOG_ACCESS,  /* This type of log entry gives a host access to a file */
};

#define TAGFS_MAX_PATHLEN 80
#define TAGFS_MAX_HOSTNAME_LEN 32

/* tagfs_fc_flags */
#define TAGFS_FC_ALL_HOSTS_RO (1 << 0)
#define TAGFS_FC_ALL_HOSTS_RW (1 << 1)

/* Maximum number of extents in a FC extent list */
#define TAGFS_FC_MAX_EXTENTS 8

/* This log entry creates a file */
struct tagfs_file_creation {
	/* TODO: consistent field naming */
	u64     tagfs_fc_size;
	u32     tagfs_nextents;
	u32     tagfs_fc_flags;

	uid_t   fc_uid;
	gid_t   fc_gid;
	mode_t  fc_mode;

	u8      tagfs_relpath[TAGFS_MAX_PATHLEN];
	struct  tagfs_log_extent tagfs_ext_list[TAGFS_FC_MAX_EXTENTS];
};

/* A log entry of type TAGFS_LOG_ACCESS contains a struct tagfs_file_access entry.
 */
struct tagfs_file_access {
	char fa_hostname[TAGFS_MAX_HOSTNAME_LEN];
	uid_t fa_uid;
	gid_t fa_gid;
	u8  fa_owner_perm;
	u8  fa_group_perm;
	u8  fa_other_perm;
};

struct tagfs_log_entry {
	//u64 tagfs_log_entry_len; /* all log entries currently the same size */
	u64 tagfs_log_entry_seqnum;
	u32 tagfs_log_entry_type; /* TAGFS_LOG_FILE_CREATION or TAGFS_LOG_ACCESS */
	//uuid_le tagfs_file_uuid;  /* TODO: use this, which I think is needed */
	union {
		struct tagfs_file_creation tagfs_fc;
		struct tagfs_file_access   tagfs_fa;
	};
};

#define TAGFS_LOG_MAGIC 0xbadcafef00d

struct tagfs_log {
	u64 tagfs_log_magic;
	u64 tagfs_log_len;
	u64 tagfs_log_next_seqnum;
	u64 tagfs_log_next_index;
	u64 tagfs_log_last_index; /* Log would overflow if we write past here */
	struct tagfs_log_entry entries[];
};

#endif /* TAGFS__META_H */
