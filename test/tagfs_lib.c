
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
#include <sys/mman.h>
#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */

#include "../tagfs/tagfs.h"
#include "../tagfs/tagfs_ioctl.h"
#include "../tagfs/tagfs_meta.h"

#include "tagfs_lib.h"

void
tagfs_uuidgen(uuid_le *uuid)
{
	uuid_t local_uuid;

	uuid_generate(local_uuid);
	memcpy(uuid, &local_uuid, sizeof(local_uuid));
}

void
tagfs_print_uuid(const uuid_le *uuid)
{
	uuid_t local_uuid;
	char uuid_str[37];

	memcpy(&local_uuid, uuid, sizeof(local_uuid));
	uuid_unparse(local_uuid, uuid_str);

	printf("%s\n", uuid_str);

}

int
tagfs_get_device_size(const char       *fname,
		      size_t           *size,
		      enum extent_type *type)
{
	char spath[PATH_MAX];
	char npath[PATH_MAX];
	char *rpath, *basename;
	FILE *sfile;
	u_int64_t size_i;
	struct stat st;
	int rc;
	int is_char = 0;

	rc = stat(fname, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to stat file %s (%s)\n",
			__func__, fname, strerror(errno));
		return -errno;
	}

	switch (st.st_mode & S_IFMT) {
	case S_IFBLK:
		printf("%s is a block device\n", fname);
		if (type)
			*type = FSDAX_EXTENT;
		break;
	case S_IFCHR:
		printf("%s character device\n", fname);
		is_char = 1;
		if (type)
			*type = DAX_EXTENT;
		break;
	default:
		fprintf(stderr, "invalid dax device %s\n", fname);
		return -EINVAL;
	}

	basename = strrchr(fname, '/');
	if (is_char) {
		/* character device */
		snprintf(spath, PATH_MAX, "/sys/dev/char/%d:%d/subsystem",
			 major(st.st_rdev), minor(st.st_rdev));

		//basename = get_basename()
		snprintf(spath, PATH_MAX, "/sys/dev/char/%d:%d/size",
			 major(st.st_rdev), minor(st.st_rdev));
	} else {
		/* It's a block device */
		snprintf(spath, PATH_MAX, "/sys/class/block/%s/size", basename);
	}
	printf("checking for size in %s\n", spath);

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

	printf("%s: size=%ld\n", __func__, size_i);
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

void print_fsinfo(const struct tagfs_superblock *sb,
		  const struct tagfs_log        *logp,
		  int                            verbose)
{
	size_t  total_log_size;

	if (verbose) {
		printf("sizeof superblock: %ld\n", sizeof(struct tagfs_superblock));
		printf("Superblock UUID:   ");
		tagfs_print_uuid(&sb->ts_uuid);
		printf("sizeof log:        %ld\n", sizeof(struct tagfs_log));
		printf("sizeof log_entry:  %ld\n", sizeof(struct tagfs_log_entry));

		printf("last_log_index:    %ld\n", logp->tagfs_log_last_index);
		total_log_size = sizeof(struct tagfs_log)
			+ (sizeof(struct tagfs_log_entry) * logp->tagfs_log_last_index);
		printf("full log size:     %ld\n", total_log_size);
		printf("TAGFS_LOG_LEN:     %ld\n", TAGFS_LOG_LEN);
		printf("Remainder:         %ld\n", TAGFS_LOG_LEN - total_log_size);
		printf("\nfc: %ld\n", sizeof(struct tagfs_file_creation));
		printf("fa:   %ld\n", sizeof(struct tagfs_file_access));
	}

}

int
tagfs_mmap_superblock_and_log(const char *devname,
			      struct tagfs_superblock **sbp,
			      struct tagfs_log **logp,
			      int read_only)
{
	int fd = 0;
	void *sb_buf;
	void *log_buf;
	int rc = 0;
	int mapmode = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;
	int openmode = (read_only) ? O_RDONLY : O_RDWR;

	fd = open(devname, openmode, 0);
	if (fd < 0) {
		fprintf(stderr, "open/create failed; rc %d errno %d\n", rc, errno);
		rc = -1;
		goto err_out;
	}

	sb_buf = mmap (0, TAGFS_SUPERBLOCK_SIZE, mapmode, MAP_SHARED, fd, 0);
	if (sb_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap superblock from %s\n", devname);
		rc = -1;
		goto err_out;
	}
	*sbp = (struct tagfs_superblock *)sb_buf;

	log_buf = mmap (0, TAGFS_LOG_LEN, mapmode, MAP_SHARED, fd, TAGFS_LOG_OFFSET);
	if (log_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap log from %s\n", devname);
		rc = -1;
		goto err_out;
	}
	*logp = (struct tagfs_log *)log_buf;
	close(fd);
	return 0;

err_out:
	if (sb_buf)
		munmap(sb_buf, TAGFS_SUPERBLOCK_SIZE);

	if (log_buf)
		munmap(log_buf, TAGFS_LOG_LEN);

	if (fd)
		close(fd);
	return rc;
}

int
tagfs_fsck(const char *devname)
{
	struct tagfs_superblock *sb;
	struct tagfs_log *logp;
	size_t size;
	int rc;

	rc = tagfs_get_device_size(devname, &size, NULL);
	if (rc < 0)
		return -1;

	printf("device: %s\n", devname);
	printf("size:   %ld\n", size);
	rc = tagfs_mmap_superblock_and_log(devname, &sb, &logp, 1 /* read-only */);
	print_fsinfo(sb, logp, 1);
	return 0;
}
