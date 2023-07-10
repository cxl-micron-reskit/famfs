#ifndef _H_TAGFS_LIB
#define _H_TAGFS_LIB

#include "../tagfs/tagfs.h"

#include "../tagfs/tagfs_meta.h"

extern int tagfs_get_device_size(const char *fname, size_t *size);
extern int tagfs_append_log(struct tagfs_log *logp, struct tagfs_log_entry *e);

extern void print_fsinfo(struct tagfs_superblock *sb, struct tagfs_log *logp, int verbose);

void tagfs_uuidgen(uuid_le *uuid);
void tagfs_print_uuid(uuid_le *uuid);

#endif /* _H_TAGFS_LIB */
