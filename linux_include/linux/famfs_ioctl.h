/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023-2024 Micron Technology, Inc.
 *
 * This file system, originally based on ramfs the dax support from xfs,
 * is intended to allow multiple host systems to mount a common file system
 * view of dax files that map to shared memory.
 */
#ifndef FAMFS_IOCTL_H
#define FAMFS_IOCTL_H

#include <linux/ioctl.h>
#include <linux/uuid.h>

#define FAMFS_KABI_VERSION 44
#define FAMFS_MAX_EXTENTS 2
#define FAMFS_MAX_STRIPS 16
#define FAMFS_IOC_MAX_INTERLEAVED_EXTENTS 4

enum famfs_file_type {
	FAMFS_REG,
	FAMFS_SUPERBLOCK,
	FAMFS_LOG,
};

/* KABI version 44 fmap structures
 *
 * The location of the memory backing for a famfs file is described by
 * struct famfs_ioc_fmap, which specifies the file_size, file_type and extent_type,
 * Followed by an extent list of the specified type.
 *
 * There are currently two extent formats: Simple and Interleaved.
 *
 * Simple extents are just (devindex, offset, length) tuples, where devindex
 * references a devdax device that must already be "registered" with famfs.
 * As of 10/2024, famfs only supports one devdax device (which is registered as
 * the backing device at mount time), so devindex=0. But multiple backing devices
 * is coming soon.
 *
 * The extent list size must be >= file_size.
 *
 * Interleaved extents merit some additional explanation. Interleaved extents
 * stripe data across a collection of strips. Each strip is a contiguous allocation
 * from a single devdax device - and is described by a simple_extent structure.
 *
 * Interleaved_extent example:
 *   ie_nstrips = 4
 *   ie_chunk_size = 2MiB
 *   ie_nbytes = 32MiB
 *
 * ┌────────────┐────────────┐────────────┐────────────┐
 * │Chunk = 0   │Chunk = 1   │Chunk = 2   │Chunk = 3   │
 * │Strip = 0   │Strip = 1   │Strip = 2   │Strip = 3   │
 * │Stripe = 0  │Stripe = 0  │Stripe = 0  │Stripe = 0  │
 * │            │            │            │            │
 * └────────────┘────────────┘────────────┘────────────┘
 * │Chunk = 4   │Chunk = 5   │Chunk = 6   │Chunk = 7   │
 * │Strip = 0   │Strip = 1   │Strip = 2   │Strip = 3   │
 * │Stripe = 1  │Stripe = 1  │Stripe = 1  │Stripe = 1  │
 * │            │            │            │            │
 * └────────────┘────────────┘────────────┘────────────┘
 * │Chunk = 8   │Chunk = 9   │Chunk = 10  │Chunk = 11  │
 * │Strip = 0   │Strip = 1   │Strip = 2   │Strip = 3   │
 * │Stripe = 2  │Stripe = 2  │Stripe = 2  │Stripe = 2  │
 * │            │            │            │            │
 * └────────────┘────────────┘────────────┘────────────┘
 *
 * * Data is laid out across chunks in chunk # order
 * * Columns are strips
 * * Strips are contiguous devdax extents, likely each coming from a different
 *   memory device
 * * Rows are stripes
 * * The number of chunks is (int)((file_size + chunk_size - 1) / chunk_size)
 *   (and obviously the last chunk could be partial)
 * * The stripe_size = (nstrips * chunk_size)
 * * chunk_num(offset) = offset / chunk_size
 * * strip_num(offset) = chunk_num(offset) % nchunks
 * * stripe_num(offset) = offset / stripe_size
 * * ...You get the idea - see the code for more details...
 *
 * Some concrete examples from the layout above:
 * * Offset 0 in the file is offset 0 in chunk 0, which is offset 0 in strip 0
 * * offset 4MiB in the file is offset 0 in chunk 2, which is offset 0 in
 *   strip 2
 * * Offset 15MiB in the file is offset 1MiB in chunk 7, which is offset 3MiB
 *   in strip 3
 *
 * Notes about this metadata format:
 *
 * * For various reasons, chunk_size must be a multiple of the applicable
 *   PAGE_SIZE
 * * Since chunk_size and nstrips are constant within an interleaved_extent,
 *   resolving a file offset to a strip offset within a single interleaved_ext
 *   is order 1.
 * * If nstrips==1, a list of interleaved_ext structures degenerates to a
 *   regular extent list (albeit with some wasted struct space). As such, we
 *   could drop the union in famfs_ioc_fmap and just keep the array of
 *   famfs_ioc_interleaved_ext.
 */
enum famfs_ioc_ext_type {
	FAMFS_IOC_EXT_SIMPLE,
	FAMFS_IOC_EXT_INTERLEAVE,
};

struct famfs_ioc_simple_extent {
	__u64 devindex;
	__u64 offset;
	__u64 len;
};

struct famfs_ioc_interleaved_ext {
	__u64 ie_nstrips;
	__u64 ie_chunk_size;
	__u64 ie_nbytes; /* Total bytes for this interleaved_ext; sum of strips may be more */
	struct famfs_ioc_simple_extent *ie_strips;
};

struct famfs_ioc_fmap {
	__u64 fioc_file_size;
	enum famfs_file_type fioc_file_type;
	__u32 fioc_ext_type; /* enum famfs_log_ext_type */
	union {  /* Make code a little more readable */
		struct {
			__u32 fioc_nextents; /* the number of simple extents */
			struct famfs_ioc_simple_extent *kse;     /* simple extent list */
		};
		struct {
			__u32 fioc_niext; /* the number of interleaved extents */
			struct famfs_ioc_interleaved_ext *kie; /* interleaved ext list */
		};
	};
};

/**
 * struct famfs_ioc_get_fmap
 *
 * This structure is a defined size, and can be used to copyout the file map, subject
 * to the following constraints:
 * * No more than FAMFS_MAX_STRIPS simple extents
 * * No more than one striped extent
 * * Striped extent contains no more than FAMFS_MAX_EXTENTS strip extents
 */
struct famfs_ioc_get_fmap {
	struct famfs_ioc_fmap iocmap;
	union {
		struct famfs_ioc_simple_extent ikse[FAMFS_MAX_EXTENTS];
		struct {
			struct famfs_ioc_interleaved_ext ikie;
			struct famfs_ioc_simple_extent kie_strips[FAMFS_MAX_STRIPS];
		} ks;
	};
};

/**
 * struct famfs_ioc_daxdev - register an additional backing daxdev
 * @fd:           an open fd to the devdax (character) device
 * @flags:        reserved; must be zero on the daxdev-open path
 * @daxdev_index: the (cluster-invariant) index this daxdev occupies in extent
 *                dev_index fields. Index 0 is the mount-time primary daxdev.
 *
 * This mirrors the payload of fuse's FUSE_DEV_IOC_DAXDEV_OPEN
 * (struct fuse_backing_map) so both daxdev-registration ioctls share one
 * structure.
 */
struct famfs_ioc_daxdev {
	__s32 fd;
	__u32 flags;
	union {
		__u64 padding;
		__u64 daxdev_index;
	};
};

#define FAMFSIOC_MAGIC 'u'

/* famfs file ioctl opcodes */
/* Version-agnostic */
#define FAMFSIOC_NOP           _IO(FAMFSIOC_MAGIC,  0x53)

/* ABI 44 */
/*
 * MAP_CREATE (0x50, reclaimed) carries the self-describing fmap message shared
 * with the fuse GET_FMAP reply (struct fuse_famfs_fmap_header, from
 * <linux/fuse.h>). A caller that issues this opcode must include that header.
 */
#define FAMFSIOC_MAP_CREATE    _IOW(FAMFSIOC_MAGIC, 0x50, struct fuse_famfs_fmap_header)
#define FAMFSIOC_MAP_GET_V2    _IOR(FAMFSIOC_MAGIC, 0x55, struct famfs_ioc_get_fmap)
#define FAMFSIOC_DAXDEV_OPEN   _IOW(FAMFSIOC_MAGIC, 0x56, struct famfs_ioc_daxdev)

#endif /* FAMFS_IOCTL_H */
