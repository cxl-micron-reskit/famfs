/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

#ifndef _H_FAMFS_LIB
#define _H_FAMFS_LIB

#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */
#include <linux/famfs_ioctl.h>

#include "famfs.h"
#include "famfs_meta.h"

struct famfs_stripe {
	u64 nbuckets; /* Single backing daxdev will be split into this many allocation buckets */
	u64 nstrips;
	u64 chunk_size;
};

#define SB_FILE_RELPATH    ".meta/.superblock"
#define LOG_FILE_RELPATH   ".meta/.log"

/* Hack due to unintended consequences of kmod v1/v2 change */
#ifndef FAMFS_KABI_VERSION
enum famfs_extent_type {
	SIMPLE_DAX_EXTENT,
	INVALID_EXTENT_TYPE,
};
#endif

int famfs_module_loaded(int verbose);
int famfs_get_role_by_dev(const char *daxdev);
void *famfs_mmap_whole_file(const char *fname, int read_only, size_t *sizep);

extern int famfs_get_device_size(const char *fname, size_t *size, enum famfs_extent_type *type);
int famfs_check_super(const struct famfs_superblock *sb);
int famfs_fsck(const char *devname, int use_mmap, int human, int force, int verbose);

int famfs_mkmeta(const char *devname, int verbose);
int famfs_logplay(const char *mpt, int use_mmap,
		  int dry_run, int client_mode, int shadow, const char *daxdev, int verbose);

int famfs_mkfile(const char *filename, mode_t mode, uid_t uid, gid_t gid, size_t size,
		 struct famfs_stripe *stripe, int verbose);

int famfs_cp_multi(int argc, char *argv[],
		   mode_t mode, uid_t uid, gid_t gid, int recursive, int verbose);
int famfs_clone(const char *srcfile, const char *destfile, int verbose);

int famfs_mkdir(const char *dirpath, mode_t mode, uid_t uid, gid_t gid, int verbose);
int famfs_mkdir_parents(const char *dirpath, mode_t mode, uid_t uid, gid_t gid, int verbose);
int famfs_mkfs(const char *daxdev, u64 log_len, int kill, int force);
int famfs_check(const char *path, int verbose);

int famfs_flush_file(const char *filename, int verbose);

int file_not_famfs(const char *fname);

/* famfs_misc.c */
void famfs_uuidgen(uuid_le *uuid);
s64 get_multiplier(const char *endptr);
void famfs_dump_logentry(const struct famfs_log_entry *le, const int index,
			 const char *prefix, int verbose);
void famfs_dump_log(struct famfs_log *logp);
void famfs_dump_super(struct famfs_superblock *sb);
int famfs_get_system_uuid(uuid_le *uuid_out);
void famfs_print_uuid(const uuid_le *uuid);

/* famfs_yaml.c */
#include <yaml.h>
int famfs_emit_file_yaml(const struct famfs_file_meta *fm, FILE *outp);
int famfs_parse_shadow_yaml(FILE *fp, struct famfs_file_meta *fm, int max_extents,
			    int max_strips, int verbose);
const char *yaml_event_str(int event_type);

#endif /* _H_FAMFS_LIB */
