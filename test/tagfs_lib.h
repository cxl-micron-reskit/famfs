#ifndef _H_TAGFS_LIB
#define _H_TAGFS_LIB

#include "../tagfs/tagfs.h"
#include "../tagfs/tagfs_meta.h"

#define SB_FILE_RELPATH    ".meta/.superblock"
#define LOG_FILE_RELPATH   ".meta/.log"

int open_log_file_writable(const char *path, size_t *sizep);
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
	struct tagfs_simple_extent *ext_list);
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
int tagfs_logplay(const char *daxdev);

int tagfs_file_create(const char *path, mode_t mode, uid_t uid, gid_t gid, size_t size);

int tagfs_cp(char *srcfile, char *destfile);

#endif /* _H_TAGFS_LIB */
