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

enum famfs_file_type {
	FAMFS_REG,
	FAMFS_SUPERBLOCK,
	FAMFS_LOG,
};

/*
 * Extent type in a famfs fmap message, and of the in-core map
 * (famfs_file_meta.fm_extent_type).
 */
enum famfs_ioc_ext_type {
	FAMFS_IOC_EXT_SIMPLE,
	FAMFS_IOC_EXT_INTERLEAVE,
};

/*
 * The FAMFSIOC_MAP_CREATE payload is a self-describing fmap message: a
 * struct famfs_ioc_fmap_header immediately followed by @nextents extent
 * records. @fmap_size gives the total message length, so a reader is
 * self-delimiting.
 *
 * For ext_type == FAMFS_IOC_EXT_SIMPLE the records are an array of
 * @nextents famfs_ioc_simple_ext. For ext_type == FAMFS_IOC_EXT_INTERLEAVE
 * each of the @nextents records is a famfs_ioc_iext header immediately
 * followed by ie_nstrips famfs_ioc_simple_ext strip extents.
 *
 * This wire layout is byte-identical to the fmap carried in a fuse famfs
 * GET_FMAP reply (the fuse_famfs_* structs in <linux/fuse.h>), so the same
 * userspace serializer emits both. famfs_fmap.c static_asserts that the two
 * layouts stay in lockstep.
 *
 * The message is self-describing (@fmap_size bounds it), so neither the extent
 * and strip counts nor the total size are capped by this ABI. The kernel
 * applies an internal sanity limit to the copy-in and returns -EFBIG for a
 * message larger than it will accept.
 */
#define FAMFS_FMAP_VERSION 1

struct famfs_ioc_simple_ext {
	__u32 se_devindex;
	__u32 reserved;
	__u64 se_offset;
	__u64 se_len;
};

struct famfs_ioc_iext {		/* interleaved (striped) extent */
	__u32 ie_nstrips;
	__u32 ie_chunk_size;
	__u64 ie_nbytes;	/* total bytes mapped by this interleaved extent */
	__u64 reserved;
};

struct famfs_ioc_fmap_header {
	__u8  file_type;	/* enum famfs_file_type */
	__u8  reserved;
	__u16 fmap_version;	/* FAMFS_FMAP_VERSION */
	__u32 ext_type;		/* enum famfs_ioc_ext_type */
	__u32 nextents;
	__u32 fmap_size;	/* total message bytes, including this header */
	__u64 file_size;
	__u64 reserved1;
};

/**
 * struct famfs_ioc_daxdev - register an additional backing daxdev by path
 * @daxdev_index:    the (cluster-invariant) index this daxdev occupies in
 *                   extent dev_index fields. Index 0 is the mount-time primary.
 * @daxdev_path:     userspace pointer to the devdax device path (e.g.
 *                   "/dev/dax0.0"); resolved in the kernel the same way the
 *                   mount primary is.
 * @daxdev_path_len: length of the path string, not counting the NUL.
 * @flags:           reserved; must be zero.
 *
 * Standalone famfs registers every daxdev by path: the mount primary comes in
 * as the mount device name, and slots 1..n come in here. (This deliberately
 * differs from fuse's fd-based FUSE_DEV_IOC_DAXDEV_OPEN; each side is uniform
 * within itself.) Passing the path by pointer keeps the struct fixed-size, so
 * longer paths never require an ABI change.
 */
struct famfs_ioc_daxdev {
	__u64 daxdev_index;
	__u64 daxdev_path;
	__u32 daxdev_path_len;
	__u32 flags;
};

/*
 * Mount-wide operation permissions, queried and modified via the
 * FAMFSIOC_{GET,SET,CLEAR}_OPTS ioctls. A set bit means the operation is
 * permitted; a clear bit means it is rejected with -EPERM. famfs denies most
 * of these by default because the userspace log, not the kernel, is
 * authoritative for a famfs instance.
 */
#define FAMFS_OPT_CREATE	(1ULL << 0)  /* create a regular file        */
#define FAMFS_OPT_MKDIR		(1ULL << 1)  /* mkdir                        */
#define FAMFS_OPT_MKNOD		(1ULL << 2)  /* mknod a special file         */
#define FAMFS_OPT_SYMLINK	(1ULL << 3)  /* create a symlink             */
#define FAMFS_OPT_LINK		(1ULL << 4)  /* hard link                    */
#define FAMFS_OPT_UNLINK	(1ULL << 5)  /* unlink a mapped file         */
#define FAMFS_OPT_RMDIR		(1ULL << 6)  /* rmdir                        */
#define FAMFS_OPT_RENAME	(1ULL << 7)  /* rename                       */
#define FAMFS_OPT_CHMOD		(1ULL << 8)  /* setattr ATTR_MODE            */
#define FAMFS_OPT_CHOWN		(1ULL << 9)  /* setattr ATTR_UID / ATTR_GID  */
#define FAMFS_OPT_TRUNCATE	(1ULL << 10) /* setattr ATTR_SIZE (resize)   */
#define FAMFS_OPT_UTIMES	(1ULL << 11) /* setattr ATTR_ATIME/ATTR_MTIME*/
#define FAMFS_OPT_WRITE		(1ULL << 12) /* write file data              */
#define FAMFS_OPT_XATTR		(1ULL << 13) /* set/remove xattrs (reserved) */
#define FAMFS_OPT_MAP_CREATE	(1ULL << 14) /* attach an fmap (MAP_CREATE)  */

#define FAMFS_OPT_ALL		(FAMFS_OPT_CREATE | FAMFS_OPT_MKDIR | \
				 FAMFS_OPT_MKNOD | FAMFS_OPT_SYMLINK | \
				 FAMFS_OPT_LINK | FAMFS_OPT_UNLINK | \
				 FAMFS_OPT_RMDIR | FAMFS_OPT_RENAME | \
				 FAMFS_OPT_CHMOD | FAMFS_OPT_CHOWN | \
				 FAMFS_OPT_TRUNCATE | FAMFS_OPT_UTIMES | \
				 FAMFS_OPT_WRITE | FAMFS_OPT_XATTR | \
				 FAMFS_OPT_MAP_CREATE)

/**
 * struct famfs_ioc_opts - operation-permission bitmap
 * @opts: for GET, the current bitmap is returned here. For SET/CLEAR, the
 *        caller-supplied mask of bits to enable/disable on input, and the
 *        resulting bitmap on return.
 */
struct famfs_ioc_opts {
	__u64 opts;
};

#define FAMFSIOC_MAGIC 'u'

/* famfs file ioctl opcodes */
#define FAMFSIOC_NOP           _IO(FAMFSIOC_MAGIC,   0x50)

/*
 * MAP_CREATE carries the self-describing fmap message - struct
 * famfs_ioc_fmap_header followed by the extent list (see above).
 */
#define FAMFSIOC_MAP_CREATE    _IOW(FAMFSIOC_MAGIC,  0x51, struct famfs_ioc_fmap_header)
#define FAMFSIOC_DAXDEV_OPEN   _IOW(FAMFSIOC_MAGIC,  0x52, struct famfs_ioc_daxdev)
#define FAMFSIOC_GET_OPTS      _IOR(FAMFSIOC_MAGIC,  0x53, struct famfs_ioc_opts)
#define FAMFSIOC_SET_OPTS      _IOWR(FAMFSIOC_MAGIC, 0x54, struct famfs_ioc_opts)
#define FAMFSIOC_CLEAR_OPTS    _IOWR(FAMFSIOC_MAGIC, 0x55, struct famfs_ioc_opts)

#endif /* FAMFS_IOCTL_H */
