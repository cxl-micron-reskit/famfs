
#define _GNU_SOURCE

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
#include <linux/ioctl.h>
#include <sys/ioctl.h>
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


void print_fsinfo(const struct tagfs_superblock *sb,
		  const struct tagfs_log        *logp,
		  int                            verbose)
{
	size_t  total_log_size;
	int i;

	if (verbose) {
		printf("sizeof superblock: %ld\n", sizeof(struct tagfs_superblock));
		printf("Superblock UUID:   ");
		tagfs_print_uuid(&sb->ts_uuid);

		printf("num_daxdevs:       %d\n", sb->ts_num_daxdevs);
		for (i=0; i<sb->ts_num_daxdevs; i++) {
			if (i==0)
				printf("primary: ");
			else
				printf("       %d: ");

			printf("%s\n", sb->ts_devlist[i].dd_daxdev);
		}

		printf("log_offset:        %ld\n", sb->ts_log_offset);
		printf("log_len:           %ld\n", sb->ts_log_len);

		printf("sizeof(log header) %ld\n", sizeof(struct tagfs_log));
		printf("sizeof(log_entry)  %ld\n", sizeof(struct tagfs_log_entry));

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

	/* Map superblock and log in one call */
	sb_buf = mmap(0, TAGFS_SUPERBLOCK_SIZE + TAGFS_LOG_LEN, mapmode, MAP_SHARED, fd, 0);
	if (sb_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap superblock and log from %s\n", devname);
		rc = -1;
		goto err_out;
	}
	*sbp = (struct tagfs_superblock *)sb_buf;

	*logp = (struct tagfs_log *)((u64)sb_buf + TAGFS_SUPERBLOCK_SIZE);
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
tagfs_check_super(const struct tagfs_superblock *sb)
{
	if (!sb)
		return -1;
	if (sb->ts_magic != TAGFS_SUPER_MAGIC)
		return -1;
	/* TODO: check crc, etc. */
	return 0;
}

int
tagfs_fsck(const char *devname,
	   int verbose)
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

	if (tagfs_check_super(sb)) {
		fprintf(stderr, "%s: no tagfs superblock on device %s\n", __func__, devname);
		return -1;
	}

	print_fsinfo(sb, logp, verbose);
	return 0;
}

#define XLEN 256

static char *
get_mpt_by_dev(const char *mtdev)
{
	FILE * fp;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	char *devstr = 0;
	int rc;
	char *answer = NULL;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		exit(EXIT_FAILURE);

	while ((read = getline(&line, &len, fp)) != -1) {
		char dev[XLEN];
		char mpt[XLEN];
		char fstype[XLEN];
		char args[XLEN];
		int x0, x1;
		char *xmpt = NULL;

		if (strstr(line, "tagfs")) {
			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, args, &x0, &x1);
			xmpt = realpath(mpt, NULL);
			if (!xmpt) {
				fprintf(stderr, "realpath(%s) errno %d\n", mpt, errno);
				continue;
			}
			if (strcmp(dev, mtdev) == 0) {
				answer = strdup(xmpt);
				free(line);
				return answer;
			}
		}

	}

	fclose(fp);
	if (line)
		free(line);
	return answer;
}

int
tagfs_mkmeta(const char *devname)
{
	struct stat st = {0};
	int rc, sbfd, logfd;;
	char *mpt = NULL;
	char dirpath[PATH_MAX];
	char sb_file[PATH_MAX];
	char log_file[PATH_MAX];
	struct tagfs_ioc_map sb_map = {0};
	struct tagfs_ioc_map log_map = {0};
	struct tagfs_superblock *sb;
	struct tagfs_log *logp;

	dirpath[0] = 0;

	/* Get mount point path */
	mpt = get_mpt_by_dev(devname);
	printf("mpt: %s\n", mpt);
	strncat(dirpath, mpt,     PATH_MAX - 1);
	strncat(dirpath, "/",     PATH_MAX - 1);
	strncat(dirpath, ".meta", PATH_MAX - 1);

	/* Create the meta directory */
	if (stat(dirpath, &st) == -1) {
		rc = mkdir(dirpath, 0700);
		if (rc) {
			fprintf(stderr, "%s: error creating directory %s\n", __func__, dirpath);
		}
	}

	/* Create the superblock file */
	strncpy(sb_file, dirpath, PATH_MAX - 1);
	strncpy(log_file, dirpath, PATH_MAX - 1);

	strncat(sb_file, "/.superblock", PATH_MAX - 1);
	strncat(log_file, "/.log", PATH_MAX - 1);

	rc = tagfs_mmap_superblock_and_log(devname, &sb, &logp, 1);
	if (rc) {
		fprintf(stderr, "%s: superblock/log accessfailed\n", __func__);
		return -1;
	}

	if (tagfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock on device %s\n", __func__, devname);
		return -1;
	}

	/* Create and allocate Superblock file */
	sbfd = open(sb_file, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	if (sbfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, sb_file);
		return -1;
	}
	sb_map.extent_type    = FSDAX_EXTENT;
	sb_map.file_size      = TAGFS_SUPERBLOCK_SIZE;
	sb_map.ext_list_count = 1;
	sb_map.ext_list = calloc(1, sizeof(struct tagfs_extent));
	sb_map.ext_list[0].offset = 0;
	sb_map.ext_list[0].len = TAGFS_SUPERBLOCK_SIZE;

	rc = ioctl(sbfd, TAGFSIOC_MAP_CREATE, &sb_map);
	if (sbfd < 0) {
		fprintf(stderr, "MAP_CREATE failed for %s; rc %d errno %d\n",
			sb_file, rc, errno);
		unlink(sb_file);
		return -1;
	}

	/* Create and allocate log file */
	logfd = open(log_file, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	if (logfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, log_file);
		return -1;
	}

	log_map.extent_type     = FSDAX_EXTENT;
	log_map.file_size       = sb->ts_log_len;
	log_map.ext_list_count  = 1;
	log_map.ext_list = calloc(1, sizeof(struct tagfs_extent));
	log_map.ext_list[0].offset = sb->ts_log_offset;
	log_map.ext_list[0].len = sb->ts_log_len;

	rc = ioctl(logfd, TAGFSIOC_MAP_CREATE, &log_map);
	if (rc) {
		fprintf(stderr, "MAP_CREATE failed for %s; rc %d errno %d\n",
			sb_file, rc, errno);
		unlink(log_file);
		return -1;
	}

	close(sbfd);
	close(logfd);
	return 0;
}
