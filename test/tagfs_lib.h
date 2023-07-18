#ifndef _H_TAGFS_LIB
#define _H_TAGFS_LIB

#include "../tagfs/tagfs.h"
#include "../tagfs/tagfs_meta.h"

#define SB_FILE_RELPATH    ".meta/.superblock"
#define LOG_FILE_RELPATH   ".meta/.log"

int open_log_file_writable(const char *path, size_t *sizep, char *mpt_out);
int tagfs_log_file_creation(
	struct tagfs_log           *logp,
	u64                         nextents,
	struct tagfs_simple_extent *ext_list,
	const char                 *path,
	mode_t                      mode,
	uid_t                       uid,
	gid_t                       gid,
	size_t                      size);
int
tagfs_file_map_create(
	const char *                path,
	int                         fd,
	size_t                      size,
	int                         nextents,
	struct tagfs_simple_extent *ext_list,
	enum tagfs_file_type        type);
int
tagfs_file_alloc(
	int         fd,
	const char *path,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid,
	u64         size);
void *mmap_whole_file(const char *fname, int read_only, size_t *sizep);

extern int tagfs_get_device_size(const char *fname, size_t *size, enum extent_type *type);
int tagfs_mmap_superblock_and_log_raw(const char *devname, struct tagfs_superblock **sbp,
				      struct tagfs_log **logp, int read_only);
extern int tagfs_append_log(struct tagfs_log *logp, struct tagfs_log_entry *e);

extern void print_fsinfo(const struct tagfs_superblock *sb, const struct tagfs_log *logp, int verbose);
int tagfs_check_super(const struct tagfs_superblock *sb);
int tagfs_fsck(const char *devname, int verbose);

void tagfs_uuidgen(uuid_le *uuid);
void tagfs_print_uuid(const uuid_le *uuid);
int tagfs_mkmeta(const char *devname);
u64 tagfs_alloc(const char *devname, u64 size);
int tagfs_logplay(const struct tagfs_log *logp, const char *mpt, int dry_run);

char *tagfs_relpath_from_fullpath(const char *mpt, char *fullpath);
int tagfs_file_create(const char *path, mode_t mode, uid_t uid, gid_t gid, size_t size);

int tagfs_cp(char *srcfile, char *destfile);
u8 *tagfs_build_bitmap(const struct tagfs_log *logp, u64 size_in,
		       u64 *size_out, u64 *errors, int verbose);

#endif /* _H_TAGFS_LIB */
