#ifndef _H_TAGFS_LIB
#define _H_TAGFS_LIB

#include "../tagfs/tagfs.h"

#include "../tagfs/tagfs_meta.h"

extern int tagfs_get_device_size(const char *fname, size_t *size);
extern int tagfs_append_log(struct tagfs_log *logp, struct tagfs_log_entry *e);

#endif /* _H_TAGFS_LIB */
