/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _H_FAMFS_LIB_INTERNAL
#define _H_FAMFS_LIB_INTERNAL

#include "famfs_meta.h"

enum lock_opt {
	NO_LOCK = 0,
	BLOCKING_LOCK,
	NON_BLOCKING_LOCK,
};

struct famfs_locked_log {
	s64               devsize;
	struct famfs_log *logp;
	int               lfd;
	u64               nbits;
	u8               *bitmap;
	char              mpt[PATH_MAX];
};


/* Only exported for unit tests */
int famfs_validate_log_header(const struct famfs_log *logp);
int __file_not_famfs(int fd);
int file_not_famfs(const char *fname);
unsigned long famfs_gen_superblock_crc(const struct famfs_superblock *sb);
unsigned long famfs_gen_log_header_crc(const struct famfs_log *logp);
int __famfs_mkfs(const char *daxdev, struct famfs_superblock *sb, struct famfs_log *logp,
		 u64 device_size, int force, int kill);
int __open_relpath(const char *path, const char *relpath, int read_only, size_t *size_out,
		   char *mpt_out, enum lock_opt lockopt, int no_fscheck);
int __famfs_cp(struct famfs_locked_log  *lp, const char *srcfile, const char *destfile,
	       mode_t mode, uid_t uid, gid_t gid, int verbose);

int
__famfs_mkfile(struct famfs_locked_log *lp, const char *filename,
	       mode_t mode, uid_t uid, gid_t gid, size_t size, int verbose);
int __famfs_mkdir(struct famfs_locked_log *lp, const char *dirpath, mode_t mode,
		  uid_t uid, gid_t gid, int verbose);
int famfs_init_locked_log(struct famfs_locked_log *lp, const char *fspath, int verbose);
int __famfs_logplay(const struct famfs_log *logp, const char *mpt, int dry_run,
		    int client_mode, int verbose);
int famfs_fsck_scan(const struct famfs_superblock *sb, const struct famfs_log *logp,
		    int human, int verbose);

#endif /* _H_FAMFS_LIB_INTERNAL */
