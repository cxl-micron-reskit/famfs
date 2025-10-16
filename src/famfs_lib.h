/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

#ifndef _H_FAMFS_LIB
#define _H_FAMFS_LIB

#include <unistd.h>
#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */
#include <linux/famfs_ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "famfs.h"
#include "famfs_meta.h"
#include "famfs_log.h"
#include "thpool.h"

#define FAMFS_YAML_MAX 16384

struct famfs_interleave_param {
	u64 nbuckets; /* Single backing daxdev will be split into this many allocation buckets */
	u64 nstrips;
	u64 chunk_size;
};

#define SB_FILE_RELPATH    ".meta/.superblock"
#define LOG_FILE_RELPATH   ".meta/.log"
#define CFG_FILE_RELPATH   ".meta/.alloc.cfg"

/* Hack due to unintended consequences of kmod v1/v2 change */
#ifndef FAMFS_KABI_VERSION
enum famfs_extent_type {
	SIMPLE_DAX_EXTENT,
	INVALID_EXTENT_TYPE,
};
#endif

enum famfs_type {
	NOT_FAMFS=0,
	FAMFS_V1,
	FAMFS_FUSE,
};

/* fuse-only functions */
char *famfs_get_shadow_root(const char *shadow_path, int verbose);
int famfs_mount_fuse(const char *realdaxdev, const char *realmpt,
		     const char *realshadow, ssize_t timeout,
		     int logplay_use_fuse, int useraccess, int default_perm,
		     int bounce_dax, int debug, int verbose);

/* famfs_lib dual v1/v2 functions */
int file_is_famfs_v1(const char *fname);
int file_is_famfs(const char *fname);

/* famfs_lib v1 functions */
int famfs_module_loaded(int verbose);
int famfs_get_role_by_dev(const char *daxdev);
void *famfs_mmap_whole_file(const char *fname, int read_only, size_t *sizep);

extern int famfs_get_device_size(const char *fname, size_t *size);
int famfs_check_super(const struct famfs_superblock *sb,
		      u64 *log_offset, u64 *log_size);
enum famfs_system_role __famfs_get_role_and_logstats(
	const struct famfs_superblock *sb, u64 *log_offsetp,
	u64 *log_sizep);
enum famfs_system_role
famfs_get_role_and_logstats(const struct famfs_superblock *sb,
			    u64 *log_offsetp, u64 *log_sizep);
int famfs_fsck(const char *devname, int use_mmap, int human, int force, 
		int nbuckets, int verbose);

int famfs_mkmeta_standalone(const char *devname, int verbose);
int __famfs_mkmeta_superblock(const char *mpt, int shadow, int verbose);
int __famfs_mkmeta_log(const char *mpt, u64 log_offset, u64 log_size,
		   enum famfs_system_role role, int shadow, int verbose);

int famfs_logplay(
	const char *mpt, int use_mmap, int dry_run, int client_mode,
	const char *shadowpath, int shadowtest, int verbose);
int famfs_dax_shadow_logplay(
	const char *shadowpath, int dry_run, int client_mode, const char *daxdev,
	int testmode, int verbose);

int famfs_mkfile(const char *filename, mode_t mode,
		 uid_t uid, gid_t gid, size_t size,
		 struct famfs_interleave_param *interleave_param, int verbose);

int famfs_cp_multi(int argc, char *argv[], mode_t mode, uid_t uid, gid_t gid,
		   struct famfs_interleave_param *s, int recursive,
		   int thread_ct, int verbose);
int famfs_clone(const char *srcfile, const char *destfile);

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
enum famfs_type famfs_get_kernel_type(int verbose);
void free_string_list(char **strings, int nstrings);
char **tokenize_string(const char *input, const char *delimiter, int *out_count);
void famfs_thpool_destroy(threadpool thp, useconds_t usec);
void log_file_mode(
	const char *caller, const char *name, const struct stat *st,
	int log_level);

/* famfs_yaml.c */
#include <yaml.h>
int famfs_emit_file_yaml(const struct famfs_log_file_meta *fm, FILE *outp);
int famfs_emit_interleave_param_yaml(const struct famfs_interleave_param *interleave_param, FILE *outp);
int famfs_parse_shadow_yaml(FILE *fp, struct famfs_log_file_meta *fm, int max_extents,
			    int max_strips, int verbose);
int famfs_parse_alloc_yaml(FILE *fp, struct famfs_interleave_param *interleave_param, int verbose);
const char *yaml_event_str(int event_type);

/* famfs_dax.c */
int famfs_bounce_daxdev(const char *devname, int verbose);

#endif /* _H_FAMFS_LIB */
