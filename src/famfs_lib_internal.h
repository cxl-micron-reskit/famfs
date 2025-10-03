/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */

#ifndef _H_FAMFS_LIB_INTERNAL
#define _H_FAMFS_LIB_INTERNAL

#include "famfs_lib.h"
#include "famfs_meta.h"

enum lock_opt {
	NO_LOCK = 0,
	BLOCKING_LOCK,
	NON_BLOCKING_LOCK,
};

enum mock_failure {
	MOCK_FAIL_NONE = 0,
	MOCK_FAIL_GENERIC,
	MOCK_FAIL_LOG_MKDIR,
	MOCK_FAIL_OPEN_SB,
	MOCK_FAIL_READ_SB,
	MOCK_FAIL_OPEN_LOG,
	MOCK_FAIL_READ_LOG,
	MOCK_FAIL_READ_FULL_LOG,
	MOCK_FAIL_ROLE,
	MOCK_FAIL_SROLE,
	MOCK_FAIL_OPEN,
	MOCK_FAIL_MMAP,
};

struct famfs_locked_log {
	s64               devsize;
	struct famfs_log *logp;
	int               lfd;
	int               famfs_type; /* FAMFS_V1 or FAMFS_FUSE */
	u8               *bitmap;
	u64               nbits;
	u64               alloc_unit;
	/* In simple linear allocations, remembering the current position
	 * speeds up repetitive allocations (under a single locked_log session)
	 * because we don't have re-iterate over the previously-allocated
	 * portion of the bitmap 
	 */
	u64               cur_pos;
	/* alloc is contiguous if nbuckets or nstrips are clear;
	 * if both are set thhe backing device is bucketized at bucket_size,
	 * and each allocation is interleaved across nstrips buckets (though
	 * smaller allocations will use fewer strips)
	 */
	struct famfs_interleave_param interleave_param;
	struct thpool_ *thp;
	char *mpt;
	char *shadow_root;
};

struct famfs_log_stats {
	u64 n_entries;
	u64 bad_entries;
	u64 f_logged;
	u64 f_existed;
	u64 f_created;
	u64 f_errs;
	u64 d_logged;
	u64 d_existed;
	u64 d_created;
	u64 d_errs;
	u64 yaml_errs;
	u64 yaml_checked;
};

/*
 * Exported for internal use
 */
/* famfs_alloc.c */
u8 *famfs_build_bitmap(
	const struct famfs_log *logp, const u64 alloc_unit, u64 dev_size_in,
	u64 *bitmap_nbits_out, u64 *alloc_errors_out, u64 *size_total_out,
	u64 *alloc_total_out, struct famfs_log_stats *log_stats_out,
	int verbose);
int famfs_file_alloc(struct famfs_locked_log *lp, u64 size,
		     struct famfs_log_fmap **fmap_out, int verbose);
void mu_print_bitmap(u8 *bitmap, int num_bits);
int famfs_validate_interleave_param(
		struct famfs_interleave_param *interleave_param,
		const u64 alloc_unit, u64 devsize, int verbose);

struct bucket_series {
	u64 nbuckets;
	u64 current;
	u64 *buckets;
};
void bucket_series_alloc(struct bucket_series **bs, u64 nbuckets, u64 start);
void bucket_series_destroy(struct bucket_series *bs);
s64 bucket_series_next(struct bucket_series *bs);
void bucket_series_rewind(struct bucket_series *bs);

/* famfs_mount.c */
char *famfs_get_mpt_by_dev(const char *mtdev);
int famfs_path_is_mount_pt(const char *path, char *dev_out, char *shadow_out);
char *find_mount_point(const char *path);

/* famfs_alloc.c */
struct famfs_bitmap_stats {
	u64 size;
	u64 bits_inuse;
	u64 bits_free;
	u64 fragments_free;
	u64 largest_free_section;
	u64 smallest_free_section;
};
void mu_bitmap_range_stats(u8 *bitmap, u64 start, u64 end, /* exclusive */
			   struct famfs_bitmap_stats *bs);

/*
 * Only exported for unit tests
 */
int famfs_validate_log_header(const struct famfs_log *logp);
int __file_is_famfs_v1(int fd);
unsigned long famfs_gen_superblock_crc(const struct famfs_superblock *sb);
unsigned long famfs_gen_log_header_crc(const struct famfs_log *logp);
int __famfs_mkfs(const char *daxdev, struct famfs_superblock *sb, struct famfs_log *logp,
		 u64 log_len, u64 device_size, int force, int kill);
int __open_relpath(const char *path, const char *relpath, int read_only, size_t *size_out, ssize_t size_in,
		   char *mpt_out, enum lock_opt lockopt, int no_fscheck);
int __famfs_cp(struct famfs_locked_log  *lp, const char *srcfile, const char *destfile,
	       mode_t mode, uid_t uid, gid_t gid, int verbose);

int
__famfs_mkfile(struct famfs_locked_log *lp, const char *filename,
	       mode_t mode, uid_t uid, gid_t gid, size_t size,
	       int open_existing, int verbose);
int __famfs_mkdir(struct famfs_locked_log *lp, const char *dirpath, mode_t mode,
		  uid_t uid, gid_t gid, int verbose);
int famfs_init_locked_log(struct famfs_locked_log *lp, const char *fspath,
			  int thread_ct, int verbose);
int famfs_release_locked_log(struct famfs_locked_log *lp, int abort,
			     int verbose);
int
__famfs_logplay(
	const char *mpt,
	const struct famfs_log *logp,
	int dry_run, int shadow, int shadowtest,
	enum famfs_system_role role, int verbose);
int famfs_fsck_scan(const struct famfs_superblock *sb,
		    const struct famfs_log *logp,
		    int human, int nbuckets, int verbose);
int famfs_create_sys_uuid_file(char *sys_uuid_file);
int famfs_get_system_uuid(uuid_le *uuid_out);
void famfs_print_role_string(int role);
int famfs_validate_log_entry(const struct famfs_log_entry *le, u64 index);
int famfs_cp(struct famfs_locked_log *lp, const char *srcfile, const char *destfile,
		mode_t mode, uid_t uid, gid_t gid, int verbose);

/* famfs_misc.c */
int check_file_exists(const char *basepath, const char *relpath,
		      int timeout, size_t expected_size, size_t *size_out,
		      int verbose);
int kernel_symbol_exists(const char *symbol_name, const char *mod_name,
			 const int verbose);
int count_open_fds(void);

/* famfs_debug.c */
int famfs_compare_log_file_meta(const struct famfs_log_file_meta *m1,
				const struct famfs_log_file_meta *m2,
				int verbose);
void dump_stack(void);
#endif /* _H_FAMFS_LIB_INTERNAL */
