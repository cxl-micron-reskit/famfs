
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>
#include <errno.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <linux/types.h>
#include <stddef.h>

#include "../tagfs/tagfs.h"
#include "../tagfs/tagfs_ioctl.h"
#include "../tagfs/tagfs_meta.h"

#include "tagfs_lib.h"

int
tagfs_get_device_size(const char *fname, size_t *size)
{
	char spath[PATH_MAX];
	char npath[PATH_MAX];
	char *rpath, *basename;
	FILE *sfile;
	u_int64_t size_i;
	struct stat st;
	int rc;

	rc = stat(fname, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to stat file %s (%s)\n",
			__func__, fname, strerror(errno));
		return -errno;
	}

	snprintf(spath, PATH_MAX, "/sys/dev/char/%d:%d/subsystem",
		 major(st.st_rdev), minor(st.st_rdev));

	rpath = realpath(spath, npath);
	if (!rpath) {
		fprintf(stderr, "%s: realpath on %s failed (%s)\n",
			__func__, spath, strerror(errno));
		return -errno;
	}

	/* Check if DAX device */
	basename = strrchr(rpath, '/');
	if (!basename || strcmp("dax", basename+1)) {
		/* Not a dax device; see if it's a block device */

		snprintf(spath, PATH_MAX, "/sys/class/block/%s/size", basename);
	} else {

		snprintf(spath, PATH_MAX, "/sys/dev/char/%d:%d/size",
			 major(st.st_rdev), minor(st.st_rdev));
	}

	sfile = fopen(spath, "r");
	if (!sfile) {
		fprintf(stderr, "%s: fopen on %s failed (%s)\n",
			__func__, spath, strerror(errno));
		return -EINVAL;
	}

	rc = fscanf(sfile, "%lu", &size_i);
	if (rc < 0) {
		fprintf(stderr, "%s: fscanf on %s failed (%s)\n",
			__func__, spath, strerror(errno));
		fclose(sfile);
		return -EINVAL;
	}

	fclose(sfile);

	*size = (size_t)size_i;
	return 0;

}

#if 0
size_t
tagfs_get_log_entry_size(struct tagfs_log_entry *le)
{
	size_t count = 0;

	/* size of struct tagfs_log_entry before union */
	count += offsetof(struct tagfs_log_entry, tagfs_fc);

	switch (le->tagfs_log_entry_type) {
	case TAGFS_LOG_FILE:
	count += 
}
#endif

/**
 * tagfs_append_log()
 *
 * @logp - pointer to struct tagfs_log in memory media
 * @e    - pointer to log entry in memory
 *
 * NOTE: this function is not re-entrant. Must hold a lock or mutex when calling this
 * function if there is any chance of re-entrancy.
 */
int
tagfs_append_log(struct tagfs_log *logp,
		 struct tagfs_log_entry *e)
{
	/* XXX This function is not re-entrant */
	if (!logp || !e)
		return EINVAL;;

	if (logp->tagfs_log_magic != TAGFS_LOG_MAGIC) {
		fprintf(stderr, "Log has invalid magic number\n");
		return EINVAL;
	}

	if (logp->tagfs_log_next_index >= logp->tagfs_log_last_index) {
		fprintf(stderr, "log is full \n");
		return E2BIG;
	}

	e->tagfs_log_entry_seqnum = logp->tagfs_log_next_seqnum++;
	memcpy(&logp->entries[logp->tagfs_log_next_index++], e, sizeof(*e));
	
	return 0;
	
}

//struct tagfs_log_entry

/*
 *XXX do this from a block read, not from the log
 */
#if 0
int
tagfs_create_superblock_file(struct tagfs_log *logp)
{
	struct tagfs_log_entry *e;
	struct tagfs_file_creation *fc = &e->tagfs_fc;

	memset(&e, 0, sizeof(e));

	e->tagfs_log_entry_type = TAGFS_LOG_FILE;
	fc->tagfs_nextents = 1;
	fc->tagfs_fc_flags |= TAGFS_FC_ALL_HOSTS_RO;
	strcpy(fc->tagfs_relpath, TAGFS_SUPERBLOCK_PATH);
	
}
#endif
