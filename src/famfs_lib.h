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
int famfs_fsck(const char *devname, int use_mmap, int human, int verbose);

void famfs_uuidgen(uuid_le *uuid);
int famfs_get_system_uuid(uuid_le *uuid_out);
int famfs_mkmeta(const char *devname);
u64 famfs_alloc(const char *devname, u64 size);
int famfs_logplay(const char *mpt, int use_mmap,
		  int dry_run, int client_mode, int shadow, const char *daxdev, int verbose);

int famfs_mkfile(const char *filename, mode_t mode, uid_t uid, gid_t gid, size_t size, int verbose);

int famfs_cp_multi(int argc, char *argv[],
		   mode_t mode, uid_t uid, gid_t gid, int recursive, int verbose);
int famfs_clone(const char *srcfile, const char *destfile, int verbose);

int famfs_mkdir(const char *dirpath, mode_t mode, uid_t uid, gid_t gid, int verbose);
int famfs_mkdir_parents(const char *dirpath, mode_t mode, uid_t uid, gid_t gid, int verbose);
int famfs_mkfs(const char *daxdev, u64 log_len, int kill, int force);
int famfs_check(const char *path, int verbose);

void famfs_dump_log(struct famfs_log *logp);
void famfs_dump_super(struct famfs_superblock *sb);
int famfs_flush_file(const char *filename, int verbose);

int file_not_famfs(const char *fname);
s64 get_multiplier(const char *endptr);

/* famfs_yaml.c */
int famfs_emit_file_yaml(const struct famfs_file_meta *fm, FILE *outp);
int famfs_parse_file_yaml(FILE *fp, struct famfs_file_meta *fm, int max_extents);

#endif /* _H_FAMFS_LIB */
