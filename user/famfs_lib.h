/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H_FAMFS_LIB
#define _H_FAMFS_LIB

#include "famfs.h"
#include "famfs_meta.h"

#define SB_FILE_RELPATH    ".meta/.superblock"
#define LOG_FILE_RELPATH   ".meta/.log"

int open_log_file_writable(const char *path, size_t *sizep, char *mpt_out);
int open_log_file_read_only(const char *path, size_t *sizep, char *mpt_out);
int famfs_log_file_creation(
	struct famfs_log           *logp,
	u64                         nextents,
	struct famfs_simple_extent *ext_list,
	const char                 *path,
	mode_t                      mode,
	uid_t                       uid,
	gid_t                       gid,
	size_t                      size);
int
famfs_file_map_create(
	const char                 *path,
	int                         fd,
	size_t                      size,
	int                         nextents,
	struct famfs_simple_extent *ext_list,
	enum famfs_file_type        type);
int
famfs_file_alloc(
	int         fd,
	const char *path,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid,
	u64         size);
void *mmap_whole_file(const char *fname, int read_only, size_t *sizep);

extern int famfs_get_device_size(const char *fname, size_t *size, enum extent_type *type);
int famfs_mmap_superblock_and_log_raw(const char *devname, struct famfs_superblock **sbp,
				      struct famfs_log **logp, int read_only);
extern int famfs_append_log(struct famfs_log *logp, struct famfs_log_entry *e);

extern int famfs_fsck_scan(const struct famfs_superblock *sb, const struct famfs_log *logp,
			    int verbose);
int famfs_check_super(const struct famfs_superblock *sb);
int famfs_fsck(const char *devname, int use_mmap, int verbose);

void famfs_uuidgen(uuid_le *uuid);
void famfs_print_uuid(const uuid_le *uuid);
int famfs_mkmeta(const char *devname);
u64 famfs_alloc(const char *devname, u64 size);
int famfs_logplay(const struct famfs_log *logp, const char *mpt, int dry_run);

char *famfs_relpath_from_fullpath(const char *mpt, char *fullpath);
int famfs_file_create(const char *path, mode_t mode, uid_t uid, gid_t gid);
int famfs_mkfile(char *filename, mode_t mode, uid_t uid, gid_t gid, size_t size);

int famfs_cp(char *srcfile, char *destfile);
u8 *famfs_build_bitmap(const struct famfs_log *logp, u64 size_in,
		       u64 *size_out, u64 *alloc_errors,
		       u64 *size_total, u64 *alloc_total, int verbose);

int __file_not_famfs(int fd);
struct famfs_simple_extent *famfs_ext_to_simple_ext(struct famfs_extent *te_list, size_t ext_count);

int famfs_dir_create(const char *mpt, const char *path, mode_t mode, uid_t uid, gid_t gid);
int famfs_mkdir(const char *dirpath, mode_t mode, uid_t uid, gid_t gid);

#endif /* _H_FAMFS_LIB */
