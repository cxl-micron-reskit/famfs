/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H_FAMFS_LIB
#define _H_FAMFS_LIB

#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */

#include "famfs.h"
#include "famfs_meta.h"

#define SB_FILE_RELPATH    ".meta/.superblock"
#define LOG_FILE_RELPATH   ".meta/.log"

void *famfs_mmap_whole_file(const char *fname, int read_only, size_t *sizep);

extern int famfs_get_device_size(const char *fname, size_t *size, enum extent_type *type);
int famfs_check_super(const struct famfs_superblock *sb);
int famfs_fsck(const char *devname, int use_mmap, int human, int verbose);

void famfs_uuidgen(uuid_le *uuid);
int famfs_get_system_uuid(uuid_le *uuid_out);
int famfs_mkmeta(const char *devname);
u64 famfs_alloc(const char *devname, u64 size);
int famfs_logplay(const char *mpt, int use_mmap,
		  int dry_run, int client_mode, int verbose);

int famfs_mkfile(const char *filename, mode_t mode, uid_t uid, gid_t gid, size_t size, int verbose);

//int famfs_cp(const char *srcfile, const char *destfile, int verbose);
int famfs_cp_multi(int argc, char *argv[],
		   mode_t mode, uid_t uid, gid_t gid, int verbose);
int famfs_clone(const char *srcfile, const char *destfile, int verbose);

int famfs_mkdir(const char *dirpath, mode_t mode, uid_t uid, gid_t gid, int verbose);
int famfs_mkdir_parents(const char *dirpath, mode_t mode, uid_t uid, gid_t gid, int verbose);
int famfs_mkfs(const char *daxdev, int kill, int force);

#ifdef FAMFS_UNIT_TEST
#include "famfs_meta.h"
/* Only exported for unit tests */
unsigned long famfs_gen_superblock_crc(const struct famfs_superblock *sb);
unsigned long famfs_gen_log_header_crc(const struct famfs_log *logp);
int __famfs_mkfs(const char *daxdev, struct famfs_superblock *sb, struct famfs_log *logp,
		 u64 device_size, int force, int kill);
int __open_relpath(const char *path, const char *relpath, int read_only, size_t *size_out,
		   char *mpt_out, int no_fscheck);
#endif
#endif /* _H_FAMFS_LIB */
