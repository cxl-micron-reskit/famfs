// SPDX-License-Identifier: GPL-2.0

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
#include <libgen.h>
#include <assert.h>
#include <sys/param.h> /* MIN()/MAX() */
#include <zlib.h>

#include "famfs.h"
#include "famfs_ioctl.h"
#include "famfs_meta.h"

#include "famfs_lib.h"
#include "bitmap.h"

static u8 *
famfs_build_bitmap(const struct famfs_log   *logp,
		   u64                       dev_size_in,
		   u64                      *bitmap_nbits_out,
		   u64                      *alloc_errors_out,
		   u64                      *size_total_out,
		   u64                      *alloc_total_out,
		   int                       verbose);
static int
famfs_dir_create(
	const char *mpt,
	const char *rpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid);

static struct famfs_superblock *famfs_map_superblock_by_path(const char *path,int read_only);
static int famfs_file_create(const char *path, mode_t mode, uid_t uid, gid_t gid, int disable_write);

static int
__file_not_famfs(int fd)
{
	int rc;

	rc = ioctl(fd, FAMFSIOC_NOP, 0);
	if (rc)
		return 1;

	return 0;
}


static void
mu_print_bitmap(u8 *bitmap, int num_bits)
{
	int i, val;
	int sum = 0;
	char linebuf[256] = {0};

	mu_bitmap_foreach(bitmap, num_bits, i, val) {
		sum += val;

		if (!(i%64)) {
			/* New line; print previous line only is there was at least one '1' in it */
			if (sum > 0) {
				printf("%s", linebuf);
				sum = 0;
			}

			/* Start over with next line */
			linebuf[0] = 0;
			sprintf(linebuf, "\n%4d: ", i); /* Put header in line */
		}

		strcat(linebuf, (val) ? "1" : "0");     /* Append a '1' or '0' */
	}
	if (sum > 0)
		printf("%s", linebuf);
	printf("\n");
}


void
famfs_uuidgen(uuid_le *uuid)
{
	uuid_t local_uuid;

	uuid_generate(local_uuid);
	memcpy(uuid, &local_uuid, sizeof(local_uuid));
}

static void
famfs_print_uuid(const uuid_le *uuid)
{
	uuid_t local_uuid;
	char uuid_str[37];

	memcpy(&local_uuid, uuid, sizeof(local_uuid));
	uuid_unparse(local_uuid, uuid_str);

	printf("%s\n", uuid_str);
}

#define SYS_UUID_PATH "/sys/devices/virtual/dmi/id/product_uuid"
int
famfs_get_system_uuid(uuid_le *uuid_out)
{
	FILE *f;
	char uuid_str[48];  /* UUIDs are 36 characters long, plus null terminator */
	uuid_t uuid;

	f = fopen(SYS_UUID_PATH, "r");
	if (!f) {
		fprintf(stderr, "%s: unable to open system uuid at %s\n",
			__func__, SYS_UUID_PATH);
		return -errno;
	}

	/* gpt */
	if (fscanf(f, "%36s", uuid_str) != 1) {
		fprintf(stderr, "%s: unable to read system uuid at %s\n", __func__, SYS_UUID_PATH);
		fclose(f);
		return -errno;
	}

	fclose(f);

	if (uuid_parse(uuid_str, uuid) == -1) {
		/* If this fails, we should check for a famfs-specific UUID file - and if
		 * that doesn't already exist we should generate the UUID and write the file
		 */
		fprintf(stderr, "%s: Error parsing UUID (%s)\n", __func__, uuid_str);
		return -EINVAL;
	}
	memcpy(uuid_out, uuid, sizeof(uuid));
	return 0;
}

/**
 * famfs_get_role()
 *
 * Check whether this host is the master or not. If not the master, it must not attempt
 * to write the superblock or log, and files will default to read-only
 */
static int
famfs_get_role(const struct famfs_superblock *sb)
{
	uuid_le my_uuid;
	int rc = famfs_get_system_uuid(&my_uuid);

	if (rc) {
		fprintf(stderr, "%s: unable to get system uuid; assuming client role\n",
			__func__);
		return FAMFS_CLIENT;
	}
	assert(sb);
	if (memcmp(&my_uuid, &sb->ts_system_uuid, sizeof(my_uuid)) == 0)
		return FAMFS_MASTER;

	return FAMFS_CLIENT;
}

int
famfs_get_device_size(const char       *fname,
		      size_t           *size,
		      enum extent_type *type)
{
	char spath[PATH_MAX];
	char *basename;
	FILE *sfile;
	u_int64_t size_i;
	struct stat st;
	int rc;
	int is_blk = 0;

	rc = stat(fname, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to stat file %s (%s)\n",
			__func__, fname, strerror(errno));
		return -errno;
	}

	basename = strrchr(fname, '/');
	switch (st.st_mode & S_IFMT) {
	case S_IFBLK:
		is_blk = 1;
		snprintf(spath, PATH_MAX, "/sys/class/block%s/size", basename);
		break;
	case S_IFCHR:
		snprintf(spath, PATH_MAX, "/sys/dev/char/%d:%d/size",
			 major(st.st_rdev), minor(st.st_rdev));
		break;
	default:
		fprintf(stderr, "invalid dax device %s\n", fname);
		return -EINVAL;
	}

	printf("%s: getting daxdev size from file %s\n", __func__, spath);
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

	if (is_blk)
		size_i *= 512; /* blkdev size is in 512b blocks */

	printf("%s: size=%ld\n", __func__, size_i);
	*size = (size_t)size_i;
	return 0;
}

/**
 * famfs_gen_superblock_crc()
 *
 * This function must be updated if any fields changes before teh crc in the superblock!
 */
unsigned long
famfs_gen_superblock_crc(const struct famfs_superblock *sb)
{
	unsigned long crc = crc32(0L, Z_NULL, 0);

	assert(sb);
	crc = crc32(crc, (const unsigned char *)&sb->ts_magic,       sizeof(sb->ts_magic));
	crc = crc32(crc, (const unsigned char *)&sb->ts_version,     sizeof(sb->ts_version));
	crc = crc32(crc, (const unsigned char *)&sb->ts_log_offset,  sizeof(sb->ts_log_offset));
	crc = crc32(crc, (const unsigned char *)&sb->ts_log_len,     sizeof(sb->ts_log_len));
	crc = crc32(crc, (const unsigned char *)&sb->ts_uuid,        sizeof(sb->ts_uuid));
	crc = crc32(crc, (const unsigned char *)&sb->ts_system_uuid, sizeof(sb->ts_system_uuid));
	return crc;
}

unsigned long
famfs_gen_log_header_crc(const struct famfs_log *logp)
{
	unsigned long crc = crc32(0L, Z_NULL, 0);

	assert(logp);
	crc = crc32(crc, (const unsigned char *)
		    &logp->famfs_log_magic, sizeof(logp->famfs_log_magic));
	crc = crc32(crc, (const unsigned char *)
		    &logp->famfs_log_len, sizeof(logp->famfs_log_len));
	crc = crc32(crc, (const unsigned char *)
		    &logp->famfs_log_last_index, sizeof(logp->famfs_log_last_index));
	return crc;
}

static unsigned long
famfs_gen_log_entry_crc(const struct famfs_log_entry *le)
{
	unsigned long crc = crc32(0L, Z_NULL, 0);
	size_t le_size = sizeof(*le);
	size_t le_crc_size = le_size - sizeof(le->famfs_log_entry_crc);

	crc = crc32(crc, (const unsigned char *)le, le_crc_size);
	return crc;
}

/**
 * famfs_fsck_scan()
 *
 * * Print info from the superblock
 * * Print log stats
 * * build the log bitmap (which scans the log) and check for errors
 */
int
famfs_fsck_scan(
	const struct famfs_superblock *sb,
	const struct famfs_log        *logp,
	int                            human,
	int                            verbose)
{
	size_t total_log_size;
	size_t effective_log_size;
	u64 nbits;
	int i;
	u64 errors = 0;
	u8 *bitmap;
	u64 alloc_sum, fsize_sum;
	u64 dev_capacity;

	assert(sb);
	assert(logp);

	dev_capacity = sb->ts_devlist[0].dd_size;
	effective_log_size = sizeof(*logp) +
		(logp->famfs_log_next_index * sizeof(struct famfs_log_entry));

	/*
	 * Print superblock info
	 */
	printf("Famfs Superblock:\n");
	printf("  Filesystem UUID: ");
	famfs_print_uuid(&sb->ts_uuid);
	printf("  System UUID:     ");
	famfs_print_uuid(&sb->ts_system_uuid);
	printf("  sizeof superblock: %ld\n", sizeof(struct famfs_superblock));
	printf("  num_daxdevs:              %d\n", sb->ts_num_daxdevs);
	for (i = 0; i < sb->ts_num_daxdevs; i++) {
		if (i == 0)
			printf("  primary: ");
		else
			printf("         %d: ", i);

		printf("%s   %ld\n",
		       sb->ts_devlist[i].dd_daxdev, sb->ts_devlist[i].dd_size);
	}

	/*
	 * print log info
	 */
	printf("\nLog stats:\n");
	printf("  # of log entriesi in use: %lld of %lld\n",
	       logp->famfs_log_next_index, logp->famfs_log_last_index + 1);
	printf("  Log size in use:          %ld\n", effective_log_size);

	/*
	 * Build the log bitmap to scan for errors
	 */
	bitmap = famfs_build_bitmap(logp,  dev_capacity, &nbits, &errors,
				    &fsize_sum, &alloc_sum, verbose);
	if (errors)
		printf("ERROR: %lld ALLOCATION COLLISIONS FOUND\n", errors);
	else {
		u64 bitmap_capacity = nbits * FAMFS_ALLOC_UNIT;
		float space_amp = (float)alloc_sum / (float)fsize_sum;
		float percent_used = 100.0 * (float)alloc_sum /  (float)bitmap_capacity;
		float agig = 1024 * 1024 * 1024;

		printf("  No allocation errors found\n\n");
		printf("Capacity:\n");
		if (!human) {
			printf("  Device capacity:         %lld\n", dev_capacity);
			printf("  Bitmap capacity:         %lld\n", bitmap_capacity);
			printf("  Sum of file sizes:       %lld\n", fsize_sum);
			printf("  Allocated bytes:         %lld\n", alloc_sum);
			printf("  Free space:              %lld\n", bitmap_capacity - alloc_sum);
		} else {
			printf("  Device capacity:         %0.2fG\n", (float)dev_capacity / agig);
			printf("  Bitmap capacity:         %0.2fG\n", (float)bitmap_capacity/ agig);
			printf("  Sum of file sizes:       %0.2fG\n", (float)fsize_sum / agig);
			printf("  Allocated space:         %.2fG\n", (float)alloc_sum / agig);
			printf("  Free space:              %.2fG\n",
			       ((float)bitmap_capacity - (float)alloc_sum) / agig);
		}
			printf("  Space amplification:     %0.2f\n", space_amp);
		printf("  Percent used:            %.1f%%\n\n", percent_used);
	}

	free(bitmap);

	if (verbose) {
		printf("Verbose:\n");
		printf("  log_offset:        %lld\n", sb->ts_log_offset);
		printf("  log_len:           %lld\n", sb->ts_log_len);

		printf("  sizeof(log header) %ld\n", sizeof(struct famfs_log));
		printf("  sizeof(log_entry)  %ld\n", sizeof(struct famfs_log_entry));

		printf("  last_log_index:    %lld\n", logp->famfs_log_last_index);
		total_log_size = sizeof(struct famfs_log)
			+ (sizeof(struct famfs_log_entry) * (1 + logp->famfs_log_last_index));
		printf("  full log size:     %ld\n", total_log_size);
		printf("  FAMFS_LOG_LEN:     %d\n", FAMFS_LOG_LEN);
		printf("  Remainder:         %ld\n", FAMFS_LOG_LEN - total_log_size);
		printf("  sizeof(struct famfs_file_creation): %ld\n",
		       sizeof(struct famfs_file_creation));
		printf("  sizeof(struct famfs_file_access):   %ld\n",
		       sizeof(struct famfs_file_access));
		printf("\n");
	}
	return errors;
}

/**
 * famfs_mmap_superblock_and_log_raw()
 *
 * This function SHOULD ONLY BE CALLED BY FSCK AND MKMETA
 *
 * The superblock and log are mapped directly from a device. Other apps should map
 * them from their meta files!
 *
 * The superblock is not validated. That is the caller's responsibility.
 *
 * @devname   - dax device name
 * @sbp
 * @logp
 * @read_only - map sb and log read-only
 */
int
famfs_mmap_superblock_and_log_raw(const char *devname,
				  struct famfs_superblock **sbp,
				  struct famfs_log **logp,
				  int read_only)
{
	int fd = 0;
	void *sb_buf;
	int rc = 0;
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	int mapmode  = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;

	fd = open(devname, openmode, 0);
	if (fd < 0) {
		fprintf(stderr, "%s: open %s failed; rc %d errno %d\n",
			__func__, devname, rc, errno);
		rc = -1;
		goto err_out;
	}

	/* Map superblock and log in one call */
	sb_buf = mmap(0, FAMFS_SUPERBLOCK_SIZE + FAMFS_LOG_LEN, mapmode, MAP_SHARED, fd, 0);
	if (sb_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap superblock and log from %s\n", devname);
		rc = -1;
		goto err_out;
	}
	*sbp = (struct famfs_superblock *)sb_buf;
	*logp = (struct famfs_log *)((u64)sb_buf + FAMFS_SUPERBLOCK_SIZE);
	close(fd);
	return 0;

err_out:
	if (sb_buf)
		munmap(sb_buf, FAMFS_SUPERBLOCK_SIZE);

	if (fd)
		close(fd);
	return rc;
}

int
famfs_check_super(const struct famfs_superblock *sb)
{
	unsigned long sbcrc;

	if (!sb)
		return -1;
	if (sb->ts_magic != FAMFS_SUPER_MAGIC)
		return -1;

	if (sb->ts_version != FAMFS_CURRENT_VERSION) {
		fprintf(stderr, "%s: superblock version=%lld (expected %lld).\n"
			"\tThis famfs_lib is not compatible with your famfs instance\n",
			__func__, sb->ts_version, (u64)FAMFS_CURRENT_VERSION);
		return -1;
	}

	sbcrc = famfs_gen_superblock_crc(sb);
	if (sb->ts_crc != sbcrc) {
		fprintf(stderr, "%s ERROR: crc mismatch in superblock!\n", __func__);
		return -1;
	}

	/* TODO: enforce crc, etc. */
	return 0;
}

#define XLEN 256

/**
 * famfs_get_mpt_by_dev()
 *
 * @mtdev = the primary dax device for a famfs file system.
 *
 * This function determines the mount point by parsing /proc/mounts to find the mount point
 * from a dax device name.
 */
static char *
famfs_get_mpt_by_dev(const char *mtdev)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int rc;
	char *answer = NULL;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return NULL;

	while ((read = getline(&line, &len, fp)) != -1) {
		char dev[XLEN];
		char mpt[XLEN];
		char fstype[XLEN];
		char args[XLEN];
		int  x0, x1;
		char *xmpt = NULL;

		if (strstr(line, "famfs")) {
			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, args, &x0, &x1);
			if (rc <= 0)
				goto out;

			xmpt = realpath(mpt, NULL);
			if (!xmpt) {
				fprintf(stderr, "realpath(%s) errno %d\n", mpt, errno);
				continue;
			}
			if (strcmp(dev, mtdev) == 0) {
				answer = strdup(xmpt);
				free(xmpt);
				free(line);
				fclose(fp);
				return answer;
			}
		}
		if (xmpt)
			free(xmpt);

	}

out:
	fclose(fp);
	if (line)
		free(line);
	return NULL;
}

/**
 * famfs_ext_to_simple_ext()
 *
 * Convert a struct famfs_extent list to struct famfs_simple_extent.
 * The output list comes from malloc() and must be freed by the caller after use.
 */
static struct famfs_simple_extent *
famfs_ext_to_simple_ext(
	struct famfs_extent *te_list,
	size_t               ext_count)
{
	struct famfs_simple_extent *se = calloc(ext_count, sizeof(*se));
	int i;

	assert(te_list);
	if (!se)
		return NULL;

	for (i = 0; i < ext_count; i++) {
		se[i].famfs_extent_offset = te_list[i].offset;
		se[i].famfs_extent_len    = te_list[i].len;
	}
	return se;
}

/**
 * famfs_file_map_create()
 *
 * This function attaches an allocated simple extent list to a file
 *
 * @path
 * @fd           - file descriptor for the file whose map will be created (already open)
 * @size
 * @nextents
 * @extent_list
 */
static int
famfs_file_map_create(
	const char                 *path,
	int                         fd,
	size_t                      size,
	int                         nextents,
	struct famfs_simple_extent *ext_list,
	enum famfs_file_type        type)
{
	struct famfs_ioc_map filemap = {0};
	int rc;
	int i;

	assert(fd > 0);

	filemap.file_type      = type;
	filemap.file_size      = size;
	filemap.extent_type    = FSDAX_EXTENT;
	filemap.ext_list_count = nextents;

	/* TODO: check for overflow (nextents > max_extents) */
	for (i = 0; i<nextents; i++) {
		filemap.ext_list[i].offset = ext_list[i].famfs_extent_offset;
		filemap.ext_list[i].len    = ext_list[i].famfs_extent_len;
	}

	rc = ioctl(fd, FAMFSIOC_MAP_CREATE, &filemap);
	if (rc)
		fprintf(stderr, "%s: failed MAP_CREATE for file %s (errno %d)\n",
			__func__, path, errno);

	return rc;
}

/**n
 * famfs_mkmeta()
 *
 * Create the meta files (.meta/.superblock and .meta/.log)) in a mounted famfs
 * file system
 *
 * @devname - primary device for a famfs file system
 */
int
famfs_mkmeta(const char *devname)
{
	struct stat st = {0};
	int rc, sbfd, logfd;
	char *mpt = NULL;
	char dirpath[PATH_MAX];
	char sb_file[PATH_MAX];
	char log_file[PATH_MAX];
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	struct famfs_simple_extent ext;

	dirpath[0] = 0;

	/* Get mount point path */
	mpt = famfs_get_mpt_by_dev(devname);
	if (!mpt) {
		fprintf(stderr, "%s: unable to resolve mount pt from dev %s\n", __func__, devname);
		return -1;
	}
	printf("mpt: %s\n", mpt);
	strncat(dirpath, mpt,     PATH_MAX - 1);
	strncat(dirpath, "/",     PATH_MAX - 1);
	strncat(dirpath, ".meta", PATH_MAX - 1);
	free(mpt);
	mpt = NULL;

	/* Create the meta directory */
	if (stat(dirpath, &st) == -1) {
		rc = mkdir(dirpath, 0700);
		if (rc)
			fprintf(stderr, "%s: error creating directory %s\n",
				__func__, dirpath);
	}

	/* Prepare full paths of superblock and log file */
	strncpy(sb_file, dirpath, PATH_MAX - 1);
	strncpy(log_file, dirpath, PATH_MAX - 1);

	strncat(sb_file, "/.superblock", PATH_MAX - 1);
	strncat(log_file, "/.log", PATH_MAX - 1);

	/* Check if superblock file already exists, and cleanup of bad */
	rc = stat(sb_file, &st);
	if (rc == 0) {
		if ((st.st_mode & S_IFMT) == S_IFREG) {
			/* Superblock file exists */
			if (st.st_size != FAMFS_SUPERBLOCK_SIZE) {
				fprintf(stderr, "%s: unlinking bad superblock file\n",
					__func__);
				unlink(sb_file);
			}
		} else {
			fprintf(stderr,
				"%s: non-regular file found where superblock expected\n",
				__func__);
			return -EINVAL;
		}
	}

	rc = famfs_mmap_superblock_and_log_raw(devname, &sb, &logp, 1);
	if (rc) {
		fprintf(stderr, "%s: superblock/log accessfailed\n", __func__);
		return -1;
	}

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock on device %s\n", __func__, devname);
		return -1;
	}

	/* Create and provide mapping for Superblock file */
	sbfd = open(sb_file, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	if (sbfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, sb_file);
		return -1;
	}

	ext.famfs_extent_offset = 0;
	ext.famfs_extent_len    = FAMFS_SUPERBLOCK_SIZE;
	rc = famfs_file_map_create(sb_file, sbfd, FAMFS_SUPERBLOCK_SIZE, 1, &ext,
				   FAMFS_SUPERBLOCK);
	if (rc)
		return -1;

	/* Check if log file already exists, and cleanup of bad */
	rc = stat(log_file, &st);
	if (rc == 0) {
		if ((st.st_mode & S_IFMT) == S_IFREG) {
			/* Log file exists; is it the right size? */
			if (st.st_size != sb->ts_log_len) {
				fprintf(stderr, "%s: unlinking bad log file\n", __func__);
				unlink(log_file);
			}
		} else {
			fprintf(stderr,
				"%s: non-regular file found where superblock expected\n",
				__func__);
			return -EINVAL;
		}
	}

	/* Create and provide mapping for log file */
	logfd = open(log_file, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	if (logfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, log_file);
		return -1;
	}

	ext.famfs_extent_offset = sb->ts_log_offset;
	ext.famfs_extent_len    = sb->ts_log_len;
	rc = famfs_file_map_create(log_file, logfd, sb->ts_log_len, 1, &ext, FAMFS_LOG);
	if (rc)
		return -1;

	close(sbfd);
	close(logfd);
	return 0;
}

/**
 * mmap_whole_file()
 *
 * @fname
 * @read_only - mmap will be read-only if true
 * @size      - size will be stored if this pointer is non-NULL
 *
 * TODO: this is only used by the cli for file verification. Move to CLI?
 */
void *
mmap_whole_file(
	const char *fname,
	int         read_only,
	size_t     *sizep)
{
	struct stat st;
	void *addr;
	int rc, fd;
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	int mapmode  = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;

	rc = stat(fname, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to stat file %s (%s)\n",
			__func__, fname, strerror(errno));
		return NULL;
	}
	if ((st.st_mode & S_IFMT) != S_IFREG) {
		fprintf(stderr, "%s: error %s is not a regular file\n", __func__, fname);
		return NULL;
	}
	if (sizep)
		*sizep = st.st_size;

	fd = open(fname, openmode, 0);
	if (fd < 0) {
		fprintf(stderr, "open %s failed; rc %d errno %d\n", fname, rc, errno);
		rc = -1;
		return NULL;
	}

	addr = mmap(0, st.st_size, mapmode, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap file %s\n", fname);
		rc = -1;
		close(fd);
		return NULL;
	}
	return addr;
}


/******/

static inline int
famfs_log_full(const struct famfs_log *logp)
{
	return (logp->famfs_log_next_index > logp->famfs_log_last_index);
}

static inline int
famfs_log_entry_fc_path_is_relative(const struct famfs_file_creation *fc)
{
	return ((strlen((char *)fc->famfs_relpath) >= 1)
		&& (fc->famfs_relpath[0] != '/'));
}

static inline int
famfs_log_entry_md_path_is_relative(const struct famfs_mkdir *md)
{
	return ((strlen((char *)md->famfs_relpath) >= 1)
		&& (md->famfs_relpath[0] != '/'));
}

static int
famfs_validate_log_header(const struct famfs_log *logp)
{
	unsigned long crc = famfs_gen_log_header_crc(logp);

	if (logp->famfs_log_magic != FAMFS_LOG_MAGIC) {
		fprintf(stderr, "%s: bad magic number in log header\n", __func__);
		return -1;
	}
	if (logp->famfs_log_crc != crc) {
		fprintf(stderr, "%s: invalid crc in log header\n", __func__);
		return -1;
	}
	return 0;
}

static int
famfs_validate_log_entry(const struct famfs_log_entry *le, u64 index)
{
	unsigned long crc;
	int errors = 0;

	if (le->famfs_log_entry_seqnum != index) {
		fprintf(stderr, "%s: bad seqnum; expect %lld found %lld\n",
			__func__, index, le->famfs_log_entry_seqnum);
		errors++;
	}
	crc = famfs_gen_log_entry_crc(le);
	if (le->famfs_log_entry_crc != crc) {
		fprintf(stderr, "%s: bad crc at log index %lld\n", __func__, index);
		errors++;
	}
	return errors;
}

/**
 * famfs_logplay()
 *
 * Play the log for a famfs file system
 *
 * @logp        - pointer to a read-only copy or mmap of the log
 * @mpt         - mount point path
 * @dry_run     - process the log but don't create the files & directories
 * @client_mode - for testing; play the log as if this is a client node, even on master
 */
int
famfs_logplay(
	const struct famfs_log *logp,
	const char             *mpt,
	int                     dry_run,
	int                     client_mode)
{
	enum famfs_system_role role;
	struct famfs_superblock *sb;
	u64 nlog = 0;
	u64 i, j;
	int rc;

	sb = famfs_map_superblock_by_path(mpt, 1 /* read-only */);
	if (!sb)
		return -1;

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock for mpt %s\n", __func__, mpt);
		return -1;
	}

	role = (client_mode) ? FAMFS_CLIENT : famfs_get_role(sb);

	if (logp->famfs_log_magic != FAMFS_LOG_MAGIC) {
		fprintf(stderr, "%s: log has bad magic number (%llx)\n",
			__func__, logp->famfs_log_magic);
		return -1;
	}

	if (famfs_validate_log_header(logp)) {
		fprintf(stderr, "%s: invalid log header\n", __func__);
		return -1;
	}

	printf("%s: log contains %lld entries\n", __func__, logp->famfs_log_next_index);
	for (i = 0; i < logp->famfs_log_next_index; i++) {
		struct famfs_log_entry le = logp->entries[i];

		if (famfs_validate_log_entry(&le, i)) {
			fprintf(stderr, "%s: invalid log entry at index %lld\n", __func__, i);
			return -1;
		}
		nlog++;
		switch (le.famfs_log_entry_type) {
		case FAMFS_LOG_FILE: {
			const struct famfs_file_creation *fc = &le.famfs_fc;
			struct famfs_simple_extent *el;
			char fullpath[PATH_MAX];
			char rpath[PATH_MAX];
			struct stat st;
			int skip_file = 0;
			int fd;

			printf("%s: %lld file=%s size=%lld\n", __func__, i,
			       fc->famfs_relpath, fc->famfs_fc_size);

			if (!famfs_log_entry_fc_path_is_relative(fc)) {
				fprintf(stderr,
					"%s: ignoring log entry; path is not relative\n",
					__func__);
				skip_file++;
			}

			/* The only file that should have an extent with offset 0
			 * is the superblock, which is not in the log. Check for files with
			 * null offset...
			 */
			for (j = 0; j < fc->famfs_nextents; j++) {
				const struct famfs_simple_extent *se = &fc->famfs_ext_list[j].se;

				if (se->famfs_extent_offset == 0) {
					fprintf(stderr,
						"%s: ERROR file %s has extent with 0 offset\n",
						__func__, fc->famfs_relpath);
					skip_file++;
				}
			}

			if (skip_file)
				continue;

			/* famfs_mkdirs(mpt, fc->famfs_relpath); */

			snprintf(fullpath, PATH_MAX - 1, "%s/%s", mpt, fc->famfs_relpath);
			realpath(fullpath, rpath);
			if (dry_run)
				continue;

			rc = stat(rpath, &st);
			if (!rc) {
				fprintf(stderr, "%s: File (%s) already exists\n",
					__func__, rpath);
				continue;
			}
			printf("%s: creating file %s mode %o\n",
			       __func__, fc->famfs_relpath, fc->fc_mode);
			fd = famfs_file_create(rpath, fc->fc_mode, fc->fc_uid, fc->fc_gid,
					       (role == FAMFS_CLIENT) ? 1 : 0);
			if (fd < 0) {
				fprintf(stderr,
					"%s: unable to create destfile (%s)\n",
					__func__, fc->famfs_relpath);

				unlink(rpath);
				continue;
			}

			/* Build extent list of famfs_simple_extent; the log entry has a
			 * different kind of extent list...
			 */
			el = calloc(fc->famfs_nextents, sizeof(*el));
			for (j = 0; j < fc->famfs_nextents; j++) {
				const struct famfs_log_extent *tle = &fc->famfs_ext_list[j];

				el[j].famfs_extent_offset = tle[j].se.famfs_extent_offset;
				el[j].famfs_extent_len    = tle[j].se.famfs_extent_len;
			}
			famfs_file_map_create(rpath, fd, fc->famfs_fc_size,
					      fc->famfs_nextents, el, FAMFS_REG);
			close(fd);
			free(el);
			break;
		}
		case FAMFS_LOG_MKDIR: {
			const struct famfs_mkdir *md = &le.famfs_md;
			char fullpath[PATH_MAX];
			char rpath[PATH_MAX];
			int skip_dir = 0;
			struct stat st;

			printf("%s: %lld mkdir=%s\n", __func__, i, md->famfs_relpath);

			if (!famfs_log_entry_md_path_is_relative(md)) {
				fprintf(stderr,
					"%s: ignoring log mkdir entry; path is not relative\n",
					__func__);
				skip_dir++;
			}

			if (skip_dir)
				continue;

			snprintf(fullpath, PATH_MAX - 1, "%s/%s", mpt, md->famfs_relpath);
			realpath(fullpath, rpath);
			if (dry_run)
				continue;

			rc = stat(rpath, &st);
			if (!rc) {
				switch (st.st_mode & S_IFMT) {
				case S_IFDIR:
					fprintf(stderr, "%s: directory (%s) already exists\n",
						__func__, rpath);
					break;

				case S_IFREG:
					fprintf(stderr,
						"%s: file (%s) exists where dir should be\n",
						__func__, rpath);
					break;

				default:
					fprintf(stderr,
						"%s: something (%s) exists where dir should be\n",
						__func__, rpath);
					break;
				}
				continue;
			}

			printf("%s: creating directory %s\n", __func__, md->famfs_relpath);
			rc = famfs_dir_create(mpt, (char *)md->famfs_relpath, md->fc_mode,
					      md->fc_uid, md->fc_gid);
			if (rc) {
				fprintf(stderr,
					"%s: error: unable to create directory (%s)\n",
					__func__, md->famfs_relpath);
			}

			break;
		}
		case FAMFS_LOG_ACCESS:
		default:
			printf("%s: invalid log entry\n", __func__);
			break;
		}
	}
	printf("%s: processed %lld log entries\n", __func__, nlog);
	return 0;
}

/**
 * famfs_append_log()
 *
 * @logp - pointer to struct famfs_log in memory media
 * @e    - pointer to log entry in memory
 *
 * NOTE: this function is not re-entrant. Must hold a lock or mutex when calling this
 * function if there is any chance of re-entrancy.
 */
static int
famfs_append_log(struct famfs_log       *logp,
		 struct famfs_log_entry *e)
{
	/* XXX This function is not re-entrant */
	if (!logp || !e)
		return -EINVAL;

	if (logp->famfs_log_magic != FAMFS_LOG_MAGIC) {
		fprintf(stderr, "Log has invalid magic number\n");
		return -EINVAL;
	}

	if (logp->famfs_log_next_index >= logp->famfs_log_last_index) {
		fprintf(stderr, "log is full\n");
		return -E2BIG;
	}

	e->famfs_log_entry_seqnum = logp->famfs_log_next_seqnum;
	e->famfs_log_entry_crc = famfs_gen_log_entry_crc(e);
	memcpy(&logp->entries[logp->famfs_log_next_index], e, sizeof(*e));

	logp->famfs_log_next_seqnum++;
	logp->famfs_log_next_index++;
	return 0;
}


/**
 * famfs_relpath_from_fullpath()
 *
 * Returns a pointer to the relpath. This pointer points within the fullpath string
 *
 * @mpt - mount point string (rationalized by realpath())
 * @fullpath
 */
static char *
famfs_relpath_from_fullpath(
	const char *mpt,
	char       *fullpath)
{
	char *relpath;

	assert(mpt);
	assert(fullpath);
	assert(strlen(fullpath) > strlen(mpt));

	if (strstr(fullpath, mpt) != fullpath) {
		/* mpt path should be a substring starting at the beginning of fullpath*/
		fprintf(stderr, "%s: failed to get relpath from mpt=%s fullpath=%s\n",
			__func__, mpt, fullpath);
		return NULL;
	}

	/* This assumes relpath() removed any duplicate '/' characters: */
	relpath = &fullpath[strlen(mpt) + 1];
	printf("%s: mpt=%s, fullpath=%s relpath=%s\n", __func__, mpt, fullpath, relpath);
	return relpath;
}

/**
 * famfs_log_file_creation()
 */
/* TODO: UI would be cleaner if this accepted a fullpath and the mpt, and did the
 * conversion itself. Then pretty much all calls would use the same stuff.
 */
static int
famfs_log_file_creation(
	struct famfs_log           *logp,
	u64                         nextents,
	struct famfs_simple_extent *ext_list,
	const char                 *relpath,
	mode_t                      mode,
	uid_t                       uid,
	gid_t                       gid,
	size_t                      size)
{
	struct famfs_log_entry le = {0};
	struct famfs_file_creation *fc = &le.famfs_fc;
	int i;

	assert(logp);
	assert(ext_list);
	assert(nextents >= 1);
	assert(relpath[0] != '/');

	if (famfs_log_full(logp)) {
		fprintf(stderr, "%s: log full\n", __func__);
		return -ENOMEM;
	}

	//le.famfs_log_entry_seqnum = logp->famfs_log_next_seqnum++; /* XXX mem ordering */
	le.famfs_log_entry_type = FAMFS_LOG_FILE;

	fc->famfs_fc_size = size;
	fc->famfs_nextents = nextents;
	fc->famfs_fc_flags = FAMFS_FC_ALL_HOSTS_RW; /* XXX hard coded access for now */

	strncpy((char *)fc->famfs_relpath, relpath, FAMFS_MAX_PATHLEN - 1);

	fc->fc_mode = mode;
	fc->fc_uid  = uid;
	fc->fc_gid  = gid;

	/* Copy extents into log entry */
	for (i = 0; i < nextents; i++) {
		struct famfs_log_extent *ext = &fc->famfs_ext_list[i];

		ext->famfs_extent_type = FAMFS_EXT_SIMPLE;
		ext->se.famfs_extent_offset = ext_list[i].famfs_extent_offset;
		ext->se.famfs_extent_len    = ext_list[i].famfs_extent_len;
	}

	return famfs_append_log(logp, &le);
}

/**
 * famfs_log_dir_creation()
 */
/* TODO: UI would be cleaner if this accepted a fullpath and the mpt, and did the
 * conversion itself. Then pretty much all calls would use the same stuff.
 */
static int
famfs_log_dir_creation(
	struct famfs_log           *logp,
	const char                 *relpath,
	mode_t                      mode,
	uid_t                       uid,
	gid_t                       gid)
{
	struct famfs_log_entry le = {0};
	struct famfs_mkdir *md = &le.famfs_md;

	assert(logp);
	assert(relpath[0] != '/');

	if (famfs_log_full(logp)) {
		fprintf(stderr, "%s: log full\n", __func__);
		return -ENOMEM;
	}

	le.famfs_log_entry_type = FAMFS_LOG_MKDIR;

	strncpy((char *)md->famfs_relpath, relpath, FAMFS_MAX_PATHLEN - 1);

	md->fc_mode = mode;
	md->fc_uid  = uid;
	md->fc_gid  = gid;

	return famfs_append_log(logp, &le);
}

/**
 * __open_relpath()
 *
 * This functionn starts with @path and ascends until @relpath is a valid
 * sub-path from the ascended subset of @path.
 *
 * This is intended for ascending from @path until (e.g.) @relpath=".meta/.superblock"
 * is valid - and opening that.
 *
 * It is also important to verify that the @relpath file is in a famfs file system,
 * but there are also (unit test) cases where it is useful to exercise this logic
 * even if the ascended @path is not in a famfs file system.
 *
 * @path       - any path within a famfs file system (from mount pt on down)
 * @relpath    - the relative path to open (relative to the mount point)
 * @read_only
 * @size_out   - File size will be returned if this pointer is non-NULL
 * @mpt_out    - Mount point will be returned if this pointer is non-NULL
 *               (the string space is assumed to be of size PATH_MAX)
 * @no_fscheck - For unit tests only - don't check whether the file with @relpath
 *               is actually in a famfs file system.
 */
static int
__open_relpath(
	const char *path,
	const char *relpath,
	int         read_only,
	size_t     *size_out,
	char       *mpt_out,
	int         no_fscheck)
{
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	char *rpath;
	char path_copy[PATH_MAX];
	char *pc = &path_copy[0];
	struct stat st;
	int rc, fd;
	int loop_ct = 15;

	/*
	 * If path does not exist, ascend canonically until we find something that does
	 * exist, or until that remaining path string is too short, or until it looks like
	 * we might be in an infinite loop
	 */
	strncpy(path_copy, path, PATH_MAX - 1);
	while (1) {
		if (strlen(pc) <= 1) {
			fprintf(stderr, "%s: path %s appears not to be in a famfs mount\n",
				__func__, path);
			return -1;
		}

		rpath = realpath(pc, NULL);
		if (rpath)
			break; /* found a valid path */
		pc = dirname(pc);
		if (--loop_ct == 0) {
			fprintf(stderr,
				"%s: bailed from possible infinite loop; path=%s path_copy=%s\n",
				__func__, path, pc);
			return -1;
		}
	}

	if (!rpath)
		return -1;

	/*
	 * At this point rpath does exist, and is a root-based path. Continue to ascend as
	 * necessary to find the mount point which contains the meta files
	 */
	while (1) {
		char fullpath[PATH_MAX] = {0};

		rc = stat(rpath, &st);
		if (rc < 0)
			goto next;
		if ((st.st_mode & S_IFMT) == S_IFDIR) {
			/* It's a dir; does it have <relpath> under it? */
			snprintf(fullpath, PATH_MAX - 1, "%s/%s", rpath, relpath);
			rc = stat(fullpath, &st);
			if ((rc == 0) && ((st.st_mode & S_IFMT) == S_IFREG)) {
				/* We found it. */
				if (size_out)
					*size_out = st.st_size;
				if (mpt_out)
					strncpy(mpt_out, rpath, PATH_MAX - 1);
				fd = open(fullpath, openmode, 0);
				free(rpath);

				/* Check whether the file we found is actually in famfs;
				 * Unit tests can disable this check but production code
				 * should not.
				 */
				if (!no_fscheck && __file_not_famfs(fd)) {
					fprintf(stderr,
						"%s: found file %s but it is not in famfs\n",
						__func__, fullpath);
					close(fd);
					return -1;
				}
				return fd;
			}
			/* no */
		}

next:
		/* Pop up one level; exit if we're at the top */
		rpath = dirname(rpath);
		if (strcmp(rpath, "/") == 0)
			break;
	}
	free(rpath);
	return -1;
}


/**
 * open_log_file(path)
 *
 * @path - any path within a famfs file system (from mount pt on down)
 *
 * This will traverse upward from path, looking for a directory containing a .meta/.log
 * If found, it opens the log.
 */
static int
__open_log_file(
	const char *path,
	int         read_only,
	size_t     *sizep,
	char       *mpt_out)
{
	return __open_relpath(path, LOG_FILE_RELPATH, read_only, sizep, mpt_out, 0);
}

int
open_log_file_read_only(
	const char *path,
	size_t     *sizep,
	char       *mpt_out)
{
	return __open_log_file(path, 1, sizep, mpt_out);
}

static int
open_log_file_writable(
	const char *path,
	size_t     *sizep,
	char       *mpt_out)
{
	return __open_log_file(path, 0, sizep, mpt_out);
}

static int
__open_superblock_file(
	const char *path,
	int         read_only,
	size_t     *sizep,
	char       *mpt_out)
{
	return __open_relpath(path, SB_FILE_RELPATH, read_only, sizep, mpt_out, 0);
}

static int
open_superblock_file_read_only(
	const char *path,
	size_t     *sizep,
	char       *mpt_out)
{
	return __open_superblock_file(path, 1, sizep, mpt_out);
}

static int
open_superblock_file_writable(
	const char *path,
	size_t     *sizep,
	char       *mpt_out)
{
	return __open_superblock_file(path, 0, sizep, mpt_out);
}

static struct famfs_superblock *
famfs_map_superblock_by_path(
	const char *path,
	int         read_only)
{
	struct famfs_superblock *sb;
	int prot = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;
	size_t sb_size;
	void *addr;
	int fd;

	fd = __open_superblock_file(path, read_only,
				    &sb_size, NULL);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to open superblock file %s for filesystem %s\n",
			__func__, read_only ? "read-only" : "writable",	path);
		return NULL;
	}
	addr = mmap(0, sb_size, prot, MAP_SHARED, fd, 0);
	close(fd);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap superblock file %s\n", __func__, path);
		return NULL;
	}
	sb = (struct famfs_superblock *)addr;
	return sb;
}

static struct famfs_log *
famfs_map_log_by_path(
	const char *path,
	int         read_only)
{
	struct famfs_log *logp;
	int prot = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;
	size_t log_size;
	void *addr;
	int fd;

	fd = __open_log_file(path, 1 /* read only */, &log_size, NULL);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to open log file for filesystem %s\n",
			__func__, path);
		return NULL;
	}
	addr = mmap(0, log_size, prot, MAP_SHARED, fd, 0);
	close(fd);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file %s\n", __func__, path);
		return NULL;
	}
	logp = (struct famfs_log *)addr;
	return logp;
}

int
famfs_fsck(
	const char *path,
	int use_mmap,
	int human,
	int verbose)
{
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	struct stat st;
	int malloc_sb_log = 0;
	size_t size;
	int rc;

	assert(path);
	assert(strlen(path) > 1);

	rc = stat(path, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to stat path %s (%s)\n",
			__func__, path, strerror(errno));
		return -errno;
	}

	/*
	 * Lots of options here;
	 * * If a dax device (either pmem or /dev/dax) we'll fsck that - but only if the fs
	 *   is not currently mounted.
	 * * If any file path from the mount point on down in a mounted famfs file system is
	 *   specified, we will find the the superblock and log files and fsck the mounted
	 *   file system.
	 */
	switch (st.st_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR: {
		char *mpt;
		/* Check if there is a mounted famfs file system on this device;
		 * fail if so - if mounted, have to fsck by mount pt rather than device
		 */
		mpt = famfs_get_mpt_by_dev(path);
		if (mpt) {
			fprintf(stderr, "%s: error - cannot fsck by device (%s) when mounted\n",
				__func__, path);
			free(mpt);
			return -EBUSY;
		}
		/* If it's a device, we'll try to mmap superblock and log from the device */
		rc = famfs_get_device_size(path, &size, NULL);
		if (rc < 0)
			return -1;

		rc = famfs_mmap_superblock_and_log_raw(path, &sb, &logp, 1 /* read-only */);
		break;
	}
	case S_IFREG:
	case S_IFDIR: {
		/*
		 * More options: default is to read the superblock and log into local buffers
		 * (which is useful to spot check that posix read is not broken). But if the
		 * use_mmap open is provided, we will mmap the superblock and logs files
		 * rather than reading them into a local buffer.
		 */
		if (use_mmap) {
			/* If it's a file or directory, we'll try to mmap the sb and
			 * log from their files
			 *
			 * Note that this tends to fail
			 */
			sb =   famfs_map_superblock_by_path(path, 1 /* read only */);
			if (!sb) {
				fprintf(stderr, "%s: failed to map superblock from file %s\n",
					__func__, path);
				return -1;
			}

			logp = famfs_map_log_by_path(path, 1 /* read only */);
			if (!logp) {
				fprintf(stderr, "%s: failed to map log from file %s\n",
					__func__, path);
				return -1;
			}
			break;
		} else {
			int sfd;
			int lfd;
			char *buf;
			int resid;
			int total = 0;

			malloc_sb_log = 1;

			sfd = open_superblock_file_read_only(path, NULL, NULL);
			if (sfd < 0) {
				fprintf(stderr, "%s: failed to open superblock file\n", __func__);
				return -1;
			}
			/* Over-allocate so we can read 2MiB multiple */
			sb = calloc(1, FAMFS_LOG_OFFSET);
			assert(sb);

			/* Read a copy of the superblock */
			rc = read(sfd, sb, FAMFS_LOG_OFFSET); /* 2MiB multiple */
			if (rc < 0) {
				free(sb);
				close(sfd);
				fprintf(stderr, "%s: error %d reading superblock file\n",
					__func__, errno);
				return -errno;
			} else if (rc < sizeof(*sb)) {
				free(sb);
				close(sfd);
				fprintf(stderr, "%s: error: short read of superblock %d/%ld\n",
					__func__, rc, sizeof(*sb));
				return -1;
			}
			close(sfd);

			lfd = open_log_file_read_only(path, NULL, NULL);
			if (lfd < 0) {
				free(sb);
				close(sfd);
				fprintf(stderr, "%s: failed to open log file\n", __func__);
				return -1;
			}

			logp = calloc(1, sb->ts_log_len);
			assert(logp);

			/* Read a copy of the log */
			resid = sb->ts_log_len;
			buf = (char *)logp;
			do {
				rc = read(lfd, &buf[total], resid);
				if (rc < 0) {
					free(sb);
					free(logp);
					close(lfd);
					fprintf(stderr, "%s: error %d reading log file\n",
						__func__, errno);
					return -errno;
				}
				if (verbose)
					printf("%s: read %d bytes of log\n", __func__, rc);

				resid -= rc;
				total += rc;
			} while (resid > 0);

			close(lfd);
		}
	}
		break;
	default:
		fprintf(stderr, "invalid path or dax device: %s\n", path);
		return -EINVAL;
	}

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid famfs superblock on device %s\n", __func__, path);
		return -1;
	}
	rc = famfs_fsck_scan(sb, logp, human, verbose);
	if (malloc_sb_log) {
		free(sb);
		free(logp);
	}
	return rc;
}

/**
 * famfs_validate_superblock_by_path()
 *
 * @path
 *
 * Validate the superblock and return the dax device size, or -1 if sb or size invalid
 */
static ssize_t
famfs_validate_superblock_by_path(const char *path)
{
	int sfd;
	void *addr;
	size_t sb_size;
	ssize_t daxdevsize;
	struct famfs_superblock *sb;

	/* XXX should be read only, but that doesn't work */
	sfd = open_superblock_file_writable(path, &sb_size, NULL);
	if (sfd < 0)
		return sfd;

	/* XXX should be read only, but that doesn't work */
	addr = mmap(0, sb_size, PROT_READ, MAP_SHARED, sfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap superblock file\n", __func__);
		close(sfd);
		return -1;
	}
	sb = (struct famfs_superblock *)addr;

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: invalid superblock\n", __func__);
		return -1;
	}
	daxdevsize = sb->ts_devlist[0].dd_size;
	munmap(sb, FAMFS_SUPERBLOCK_SIZE);
	close(sfd);
	return daxdevsize;
}

/**
 * put_sb_log_into_bitmap()
 *
 * The two files that are not in the log are the superblock and the log. So these
 * files need to be manually added to the allocation bitmap. This function does that.
 */
static inline void
put_sb_log_into_bitmap(u8 *bitmap)
{
	int i;

	/* Mark superblock and log as allocated */
	mu_bitmap_set(bitmap, 0);

	for (i = 1; i < ((FAMFS_LOG_OFFSET + FAMFS_LOG_LEN) / FAMFS_ALLOC_UNIT); i++)
		mu_bitmap_set(bitmap, i);
}

/**
 * famfs_build_bitmap()
 *
 * XXX: this is only aware of the first daxdev in the superblock's list
 * @logp
 * @size_in          - total size of allocation space in bytes
 * @bitmap_nbits_out - output: size of the bitmap
 * @alloc_errors_out - output: number of times a file referenced a bit that was already set
 * @fsize_total_out  - output: if ptr non-null, this is the sum of the file sizes
 * @alloc_sum_out    - output: if ptr non-null, this is the sum of all allocation sizes
 *                    (excluding double-allocations; space amplification is
 *                     @alloc_sum / @size_total provided there are no double allocations,
 *                     b/c those will increase size_total but not alloc_sum)
 * @verbose
 */
/* XXX: should get log size from superblock */
static u8 *
famfs_build_bitmap(const struct famfs_log   *logp,
		   u64                       dev_size_in,
		   u64                      *bitmap_nbits_out,
		   u64                      *alloc_errors_out,
		   u64                      *fsize_total_out,
		   u64                      *alloc_sum_out,
		   int                       verbose)
{
	u64 nbits = (dev_size_in - FAMFS_SUPERBLOCK_SIZE - FAMFS_LOG_LEN) / FAMFS_ALLOC_UNIT;
	u64 bitmap_nbytes = mu_bitmap_size(nbits);
	u8 *bitmap = calloc(1, bitmap_nbytes);
	u64 errors = 0;
	u64 alloc_sum = 0;
	u64 fsize_sum  = 0;
	int i, j;
	int rc;

	if (verbose)
		printf("%s: dev_size %lld nbits %lld bitmap_nbytes %lld\n",
		       __func__, dev_size_in, nbits, bitmap_nbytes);

	if (!bitmap)
		return NULL;

	put_sb_log_into_bitmap(bitmap);

	if (verbose) {
		printf("%s: superblock and log in bitmap:", __func__);
		mu_print_bitmap(bitmap, nbits);
	}
	/* This loop is over all log entries */
	for (i = 0; i < logp->famfs_log_next_index; i++) {
		const struct famfs_log_entry *le = &logp->entries[i];

		/* TODO: validate log sequence number */

		switch (le->famfs_log_entry_type) {
		case FAMFS_LOG_FILE: {
			const struct famfs_file_creation *fc = &le->famfs_fc;
			const struct famfs_log_extent *ext = fc->famfs_ext_list;

			fsize_sum += fc->famfs_fc_size;
			if (verbose)
				printf("%s: file=%s size=%lld\n", __func__,
				       fc->famfs_relpath, fc->famfs_fc_size);

			/* For each extent in this log entry, mark the bitmap as allocated */
			for (j = 0; j < fc->famfs_nextents; j++) {
				s64 page_num;
				s64 np;
				s64 k;

				assert(!(ext[j].se.famfs_extent_offset % FAMFS_ALLOC_UNIT));
				page_num = ext[j].se.famfs_extent_offset / FAMFS_ALLOC_UNIT;
				np = (ext[j].se.famfs_extent_len + FAMFS_ALLOC_UNIT - 1)
					/ FAMFS_ALLOC_UNIT;

				for (k = page_num; k < (page_num + np); k++) {
					rc = mu_bitmap_test_and_set(bitmap, k);
					if (rc == 0) {
						errors++; /* bit was already set */
					} else {
						/* Don't count double allocations */
						alloc_sum += FAMFS_ALLOC_UNIT;
					}
				}
			}
			break;
		}
		case FAMFS_LOG_MKDIR:
			/* Ignore directory log entries - no space is used */

			break;
		case FAMFS_LOG_ACCESS:
		default:
			printf("%s: invalid log entry\n", __func__);
			break;
		}
	}
	if (bitmap_nbits_out)
		*bitmap_nbits_out = nbits;
	if (alloc_errors_out)
		*alloc_errors_out = errors;
	if (fsize_total_out)
		*fsize_total_out = fsize_sum;
	if (alloc_sum_out)
		*alloc_sum_out = alloc_sum;
	return bitmap;
}

/**
 * bitmap_alloc_contiguous()
 *
 * @bitmap
 * @nbits - number of bits in the bitmap
 * @size - size to allocate in bytes (must convert to bits)
 *
 * Return value: the offset in bytes
 */
static u64
bitmap_alloc_contiguous(u8 *bitmap,
			u64 nbits,
			u64 size)
{
	u64 i, j;
	u64 alloc_bits = (size + FAMFS_ALLOC_UNIT - 1) /  FAMFS_ALLOC_UNIT;
	u64 bitmap_remainder;

	for (i = 0; i < nbits; i++) {
		/* Skip bits that are set... */
		if (mu_bitmap_test(bitmap, i))
			continue;

		bitmap_remainder = nbits - i;
		if (alloc_bits > bitmap_remainder) /* Remaining space is not enough */
			return 0;

		for (j = i; j < (i+alloc_bits); j++) {
			if (mse_bitmap_test32(bitmap, j))
				goto next;
		}
		/* If we get here, we didn't hit the "continue" which means that bits
		 * i-(i+alloc_bits) are available
		 */
		for (j = i; j < (i+alloc_bits); j++)
			mse_bitmap_set32(bitmap, j);

		return i * FAMFS_ALLOC_UNIT;
next:
	}
	fprintf(stderr, "%s: alloc failed\n", __func__);
	return 0;
}

/**
 * famfs_alloc_bypath()
 *
 * @path    - a path within the famfs file system
 * @size    - size in bytes
 *
 * XXX currently only contiuous allocations are supported
 */
static s64
famfs_alloc_bypath(
	struct famfs_log *logp,
	const char       *path,
	u64               size,
	int               verbose)
{
	ssize_t daxdevsize;
	u8 *bitmap;
	u64 nbits;
	u64 offset;

	if (size <= 0)
		return -1;

	daxdevsize = famfs_validate_superblock_by_path(path);
	if (daxdevsize < 0)
		return daxdevsize;

	bitmap = famfs_build_bitmap(logp, daxdevsize, &nbits, NULL, NULL, NULL, 0);
	if (verbose) {
		printf("\nbitmap before:\n");
		mu_print_bitmap(bitmap, nbits);
	}
	offset = bitmap_alloc_contiguous(bitmap, nbits, size);
	if (verbose) {
		printf("\nbitmap after:\n");
		mu_print_bitmap(bitmap, nbits);
		printf("\nAllocated offset: %lld\n", offset);
	}
	free(bitmap);
	return offset;
}

/**
 * famfs_file_alloc()
 *
 * Alllocate space for a file, making it ready to use
 *
 * @fd
 * @path - full path of file to allocate
 * @mode -
 * @uid
 * @size - size to alloacte
 */
static int
famfs_file_alloc(
	int         fd,
	const char *path,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid,
	u64         size,
	int         verbose)
{
	struct famfs_simple_extent ext = {0};
	struct famfs_log *logp;
	char mpt[PATH_MAX];
	size_t log_size;
	char *relpath;
	char *rpath;
	s64 offset;
	void *addr;
	int lfd;
	int rc;

	assert(fd > 0);

	rpath = realpath(path, NULL);
	/* Log file */
	lfd = open_log_file_writable(rpath, &log_size, mpt);
	if (lfd < 0) {
		/* If we can't open the log file for writing, don't allocate */
		free(rpath);
		return lfd;
	}

	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		close(lfd);
		return -1;
	}
	close(lfd);
	lfd = 0;
	logp = (struct famfs_log *)addr;

	/* For the log, we need the path relative to the mount point.
	 * getting this before we allocate is cleaner if the path is sombhow bogus
	 */
	relpath = famfs_relpath_from_fullpath(mpt, rpath);
	if (!relpath)
		return -EINVAL;

	/* Allocation is always contiguous initially */
	offset = famfs_alloc_bypath(logp, rpath, size, verbose);
	if (offset < 0) {
		rc = -ENOMEM;
		goto out;
	}

	ext.famfs_extent_len    = round_size_to_alloc_unit(size);
	ext.famfs_extent_offset = offset;

	rc = famfs_log_file_creation(logp, 1, &ext,
				     relpath, mode, uid, gid, size);
	if (rc)
		goto out;

	rc =  famfs_file_map_create(path, fd, size, 1, &ext, FAMFS_REG);
out:
	if (lfd > 0)
		close(lfd);
	if (rpath)
		free(rpath);
	return rc;
}

/**
 * famfs_file_create()
 *
 * Create a file but don't allocate dax space yet
 *
 * @path
 * @mode
 * @uid  - used if both uid and gid are non-null
 * @gid  - used if both uid and gid are non-null
 * @disable_write - if this flag is non-zero, write permissions will be removed from the mode
 *                  (we default files to read-only on client systems)
 *
 * Returns a file descriptior or -EBADF if the path is not in a famfs file system
 *
 * TODO: append "_empty" to function name
 */
static int
famfs_file_create(const char *path,
		  mode_t      mode,
		  uid_t       uid,
		  gid_t       gid,
		  int         disable_write)
{
	struct stat st;
	int rc = 0;
	int fd;

	rc = stat(path, &st);
	if (rc == 0) {
		fprintf(stderr, "%s: file already exists: %s\n", __func__, path);
		return -1;
	}

	if (disable_write)
		mode = mode & ~(S_IWUSR | S_IWGRP | S_IWOTH);

	fd = open(path, O_RDWR | O_CREAT, mode); /* TODO: open as temp file,
						  * move into place after alloc
						  */
	if (fd < 0) {
		fprintf(stderr, "%s: open/creat %s failed fd %d\n",
			__func__, path, fd);
		return fd;
	}

	if (__file_not_famfs(fd)) {
		close(fd);
		unlink(path);
		fprintf(stderr, "%s: file %s not in a famfs mount\n",
			__func__, path);
		return -EBADF;
	}

	if (uid && gid) {
		rc = fchown(fd, uid, gid);
		if (rc)
			fprintf(stderr, "%s: fchown returned %d errno %d\n",
				__func__, rc, errno);
	}
	return fd;
}

/**
 * famfs_mkfile()
 *
 * Create *and* allocate a file
 *
 * Returns an open file descriptor if successful.
 */
int
famfs_mkfile(const char    *filename,
	     mode_t   mode,
	     uid_t    uid,
	     gid_t    gid,
	     size_t   size,
	     int      verbose)
{
	enum famfs_system_role role;
	struct famfs_superblock *sb;
	char fullpath[PATH_MAX];
	int fd, rc;

	/*
	 * Check system role; files can only be created on FAMFS_MASTER system
	 */
	sb = famfs_map_superblock_by_path(filename, 1 /* read-only */);
	if (!sb)
		return -1;

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock for path %s\n", __func__, filename);
		return -1;
	}
	role = famfs_get_role(sb);

	if (role != FAMFS_MASTER) {
		fprintf(stderr, "%s: file creation not allowed on client systems\n", __func__);
		return -EPERM;
	}

	/* Create the file */
	fd = famfs_file_create(filename, mode, uid, gid, 0);
	if (fd < 0)
		return fd;

	/* Clean up the filename path. (Can't call realpath until the file exists) */
	if (realpath(filename, fullpath) == NULL) {
		fprintf(stderr, "%s: realpath() unable to rationalize filename %s\n",
			__func__, filename);
		close(fd);
		unlink(filename);
		return -EBADF;
	}

	rc = famfs_file_alloc(fd, fullpath, mode, uid, gid, size, verbose);
	if (rc) {
		fprintf(stderr, "%s: famfs_file_alloc(%s, size=%ld) failed\n",
			__func__, fullpath, size);
		close(fd);
		unlink(fullpath);
		return -1;
	}
	return fd;
}

/**
 * famfs_dir_create()
 *
 * Create a directory
 *
 * @mpt
 * @path
 * @mode
 * @uid  - used if both uid and gid are non-null
 * @gid  - used if both uid and gid are non-null
 * @size
 *
 *
 */
static int
famfs_dir_create(
	const char *mpt,
	const char *rpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid)
{
	int rc = 0;
	char fullpath[PATH_MAX];

	snprintf(fullpath, PATH_MAX - 1, "%s/%s", mpt, rpath);
	rc = mkdir(fullpath, mode);
	if (rc) {
		fprintf(stderr, "%s: failed to mkdir %s\n", __func__, fullpath);
		return -1;
	}

	/* Check if dir is in famfs mount? */

	if (uid && gid) {
		rc = chown(fullpath, uid, gid);
		if (rc)
			fprintf(stderr, "%s: chown returned %d errno %d\n",
				__func__, rc, errno);
		return -1;
	}
	return 0;
}

/**
 * libfamfs:
 *
 * famfs_cp(srcfile, destfile)
 */

int
famfs_mkdir(
	const char *dirpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid)
{
	int rc;

	char realparent[PATH_MAX];
	char fullpath[PATH_MAX];
	char mpt_out[PATH_MAX];
	struct famfs_log *logp;
	char *dirdupe   = NULL;
	char *parentdir = NULL;
	char *basedupe  = NULL;
	char *newdir    = NULL;
	char *relpath   = NULL;
	size_t log_size;
	struct stat st;
	void *addr;
	int lfd;

	dirdupe  = strdup(dirpath);  /* call dirname() on this dupe */
	basedupe = strdup(dirpath); /* call basename() on this dupe */
	newdir   = basename(basedupe);

	/* full dirpath should not exist, but the parentdir path must exist and be a directory */
	parentdir = dirname(dirdupe);
	if (strcmp(parentdir, ".") == 0) {
		fprintf(stderr, "%s: bad dirpath %s\n", __func__, dirpath);
		rc = -1;
		goto err_out;
	}
	rc = stat(parentdir, &st);
	if ((st.st_mode & S_IFMT) != S_IFDIR) {
		fprintf(stderr, "%s: parent (%s) of path %s is not a directory\n",
			__func__, dirpath, parentdir);
		rc = -1;
		goto err_out;
	}

	/* parentdir exists and is a directory; rationalize the path with realpath */
	if (realpath(parentdir, realparent) == 0) {
		fprintf(stderr, "%s: failed to rationalize parentdir path (%s)\n",
			__func__, parentdir);
		rc = -1;
		goto err_out;
	}

	/* Rebuild full path of to-be-createed directory from the rationalized parent dir path */
	rc = snprintf(fullpath, PATH_MAX - 1, "%s/%s", realparent, newdir);
	if (rc < 0) {
		fprintf(stderr, "%s: fullpath overflow\n", __func__);
		goto err_out;
	}

	/*
	 * For this operation we need to open the log file, which also gets us
	 * the mount point path
	 */
	lfd  = open_log_file_writable(realparent, &log_size, mpt_out);
	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		rc = -1;
		goto err_out;
	}
	close(lfd);
	lfd  = 0;
	logp = (struct famfs_log *)addr;

	printf("%s: creating directory %s\n", __func__, fullpath);

	relpath = famfs_relpath_from_fullpath(mpt_out, fullpath);
	rc = famfs_dir_create(mpt_out, relpath, mode, uid, gid);
	if (rc) {
		fprintf(stderr, "%s: failed to mkdir %s\n", __func__, fullpath);
		rc = -1;
		goto err_out;
	}

	/* log dir creation */
	rc = famfs_log_dir_creation(logp, relpath, mode, uid, gid);

err_out:
	if (dirdupe)
		free(dirdupe);
	if (basedupe)
		free(basedupe);

	return rc;
}

int
famfs_cp(const char *srcfile,
	 const char *destfile,
	 int   verbose)
{
	struct stat srcstat;
	struct stat deststat;
	int rc, srcfd, destfd;
	char *destp;

	size_t chunksize, remainder, offset;
	ssize_t bytes;

	/**
	 * Check the destination file first, since that is constrained in several ways:
	 * * Dest must be in a famfs file system
	 * * Dest must not exist already
	 */
	rc = stat(destfile, &deststat);
	if (!rc) {
		fprintf(stderr, "%s: error: dest destfile (%s) exists\n", __func__, destfile);
		return rc;
	}
	rc = stat(srcfile, &srcstat);
	if (rc) {
		fprintf(stderr, "%s: unable to stat srcfile (%s)\n", __func__, srcfile);
		return rc;
	}

	/* XXX: check wihether new file will fit! */

	/*
	 * Makefsure we can open and read the source file
	 */
	srcfd = open(srcfile, O_RDONLY, 0);
	if (srcfd < 0) {
		fprintf(stderr, "%s: unable to open srcfile (%s)\n", __func__, srcfile);
		unlink(destfile);
		return rc;
	}

	destfd = famfs_mkfile(destfile, srcstat.st_mode, srcstat.st_uid,
			      srcstat.st_gid, srcstat.st_size, verbose);

	destp = mmap(0, srcstat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, destfd, 0);
	if (destp == MAP_FAILED) {
		fprintf(stderr, "%s: dest mmap failed\n", __func__);
		unlink(destfile);
		return -1; /* XXX */
	}

	/* Copy the data */
	chunksize = 0x100000; /* 1 MiB copy chunks */
	offset = 0;
	remainder = srcstat.st_size;
	for ( ; remainder > 0; ) {
		size_t cur_chunksize = MIN(chunksize, remainder);

		/* read into mmapped destination */
		bytes = read(srcfd, &destp[offset], cur_chunksize);
		if (bytes < 0) {
			fprintf(stderr, "%s: copy fail: "
				"ofs %ld cur_chunksize %ld remainder %ld\n",
				__func__, offset, cur_chunksize, remainder);
			printf("rc=%ld errno=%d\n", bytes, errno);
			return -1;
		}
		if (bytes < cur_chunksize) {
			fprintf(stderr, "%s: short read: "
				"ofs %ld cur_chunksize %ld remainder %ld\n",
				__func__, offset, cur_chunksize, remainder);
		}

		/* Update offset and remainder */
		offset += bytes;
		remainder -= bytes;
	}

	close(srcfd);
	close(destfd);
	return 0;
}

/**
 * famfs_clone()
 *
 *
 * This function is for generating cross-linked file errors, and should be compiled out
 * of the library when not needed for that purpose.
 */
int
famfs_clone(const char *srcfile,
	    const char *destfile,
	    int   verbose)
{
	struct famfs_superblock *sb;//`, *sb2;
	struct famfs_ioc_map filemap = {0};
	struct famfs_extent *ext_list;
	char srcfullpath[PATH_MAX];
	char destfullpath[PATH_MAX];
	int lfd = 0;
	int sfd = 0;
	int dfd = 0;
	char mpt_out[PATH_MAX];
	char *relpath;
	struct famfs_log *logp;
	void *addr;
	size_t log_size;
	struct famfs_simple_extent *se;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	mode_t mode = S_IRUSR|S_IWUSR;
	enum famfs_system_role role;
	int rc;

	/* srcfile must already exist */
	if (realpath(srcfile, srcfullpath) == NULL) {
		fprintf(stderr, "%s: bad source path %s\n", __func__, srcfile);
		return -1;
	}

	/*
	 * Check system role; files can only be created on FAMFS_MASTER system
	 */
	sb = famfs_map_superblock_by_path(srcfullpath, 1 /* read-only */);
	if (!sb)
		return -1;

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock for path %s\n", __func__, srcfullpath);
		return -1;
	}
	role = famfs_get_role(sb);

	if (role != FAMFS_MASTER) {
		fprintf(stderr, "%s: file creation not allowed on client systems\n", __func__);
		return -EPERM;
	}

	/* Make sure both files are in the SAME famfs file system */

	/*
	 * Open source file and make sure it's a famfs file
	 */
	sfd = open(srcfullpath, O_RDONLY, 0);
	if (sfd < 0) {
		fprintf(stderr, "%s: failed to open source file %s\n",
			__func__, srcfullpath);
		return -1;
	}
	if (__file_not_famfs(sfd)) {
		fprintf(stderr, "%s: source file %s is not a famfs file\n",
			__func__, srcfullpath);
		return -1;
	}

	/*
	 * Get map for source file
	 */
	rc = ioctl(sfd, FAMFSIOC_MAP_GET, &filemap);
	if (rc) {
		fprintf(stderr, "%s: MAP_GET returned %d errno %d\n", __func__, rc, errno);
		goto err_out;
	}
	ext_list = calloc(filemap.ext_list_count, sizeof(struct famfs_extent));
	rc = ioctl(sfd, FAMFSIOC_MAP_GETEXT, ext_list);
	if (rc) {
		fprintf(stderr, "%s: GETEXT returned %d errno %d\n", __func__, rc, errno);
		goto err_out;
	}

	/*
	 * For this operation we need to open the log file, which also gets us
	 * the mount point path
	 */
	lfd = open_log_file_writable(srcfullpath, &log_size, mpt_out);
	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		rc = -1;
		goto err_out;
	}
	close(lfd);
	lfd = 0;
	logp = (struct famfs_log *)addr;

	/* Create the destination file. This will be unlinked later if we don't get all
	 * the way through the operation.
	 */
	dfd = famfs_file_create(destfile, mode, uid, gid, 0);
	if (dfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, destfile);
		rc = -1;
		goto err_out;
	}

	/*
	 * Create the file before logging, so we can avoid a BS log entry if the
	 * kernel rejects the caller-supplied allocation ext list
	 */
	/* Ugh need to unify extent types... XXX */
	se = famfs_ext_to_simple_ext(ext_list, filemap.ext_list_count);
	if (!se) {
		rc = -ENOMEM;
		goto err_out;
	}
	rc = famfs_file_map_create(destfile, dfd, filemap.file_size, filemap.ext_list_count,
				   se, FAMFS_REG);
	if (rc) {
		fprintf(stderr, "%s: failed to create destination file\n", __func__);
		exit(-1);
	}

	/* Now have created the destionation file (and therefore we know it is in a famfs
	 * mount, we need its relative path of
	 */
	if (realpath(destfile, destfullpath) == NULL) {
		close(dfd);
		unlink(destfullpath);
		return -1;
	}
	relpath = famfs_relpath_from_fullpath(mpt_out, destfullpath);
	if (!relpath) {
		rc = -1;
		unlink(destfullpath);
		goto err_out;
	}

	/* XXX - famfs_log_file_creation should only be called outside
	 * famfs_lib.c if we are intentionally doing extent list allocation
	 * bypassing famfs_lib. This is useful for testing, by generating
	 * problematic extent lists on purpoose...
	 */
	rc = famfs_log_file_creation(logp, filemap.ext_list_count, se,
				     relpath, O_RDWR, uid, gid, filemap.file_size);
	if (rc) {
		fprintf(stderr,
			"%s: failed to log caller-specified allocation\n",
			__func__);
		rc = -1;
		unlink(destfullpath);
		goto err_out;
	}
	/***************/

	close(rc);

	return 0;
err_out:
	if (lfd > 0)
		close(lfd);
	if (sfd > 0)
		close(sfd);
	if (lfd > 0)
		close(lfd);
	if (dfd > 0)
		close(dfd);
	return rc;
}

/**
 * __famfs_mkfs()
 *
 * This handller can be called by unit tests; the actual device open/mmap is
 * done by the caller, so an alternate caller can arrange for a superblock and log
 * to be written to alternate files/locations.
 */
int
__famfs_mkfs(const char              *daxdev,
	     struct famfs_superblock *sb,
	     struct famfs_log        *logp,
	     u64                      device_size,
	     int                      force,
	     int                      kill)

{
	int rc;

	if ((famfs_check_super(sb) == 0) && !force) {
		fprintf(stderr, "Device %s already has a famfs superblock\n", daxdev);
		return -1;
	}

	if (kill) {
		printf("Famfs superblock killed\n");
		sb->ts_magic      = 0;
		return 0;
	}

	rc = famfs_get_system_uuid(&sb->ts_system_uuid);
	if (rc) {
		fprintf(stderr, "mkfs.famfs: unable to get system uuid");
		return -1;
	}
	sb->ts_magic      = FAMFS_SUPER_MAGIC;
	sb->ts_version    = FAMFS_CURRENT_VERSION;
	sb->ts_log_offset = FAMFS_LOG_OFFSET;
	sb->ts_log_len    = FAMFS_LOG_LEN;
	famfs_uuidgen(&sb->ts_uuid);

	/* Configure the first daxdev */
	sb->ts_num_daxdevs = 1;
	sb->ts_devlist[0].dd_size = device_size;
	strncpy(sb->ts_devlist[0].dd_daxdev, daxdev, FAMFS_DEVNAME_LEN);

	/* Calculate superblock crc */
	sb->ts_crc = famfs_gen_superblock_crc(sb); /* gotta do this last! */

	/* Zero and setup the log */
	memset(logp, 0, FAMFS_LOG_LEN);
	logp->famfs_log_magic      = FAMFS_LOG_MAGIC;
	logp->famfs_log_len        = FAMFS_LOG_LEN;
	logp->famfs_log_next_seqnum    = 0;
	logp->famfs_log_next_index = 0;
	logp->famfs_log_last_index = ((FAMFS_LOG_LEN - offsetof(struct famfs_log, entries))
				      / sizeof(struct famfs_log_entry));

	logp->famfs_log_crc = famfs_gen_log_header_crc(logp);
	famfs_fsck_scan(sb, logp, 1, 0);
	return 0;
}

int
famfs_mkfs(const char *daxdev,
	   int         kill,
	   int         force)
{
	int rc;
	size_t devsize;
	enum extent_type type = HPA_EXTENT;
	struct famfs_superblock *sb;
	struct famfs_log *logp;

	rc = famfs_get_device_size(daxdev, &devsize, &type);
	if (rc)
		return -1;

	printf("devsize: %ld\n", devsize);

	/* XXX Get role first via read-only sb. If daxdev contains a fs that was not
	 * created on this host, fail unless force is specified
	 */

	rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, &logp, 0 /* read/write */);
	if (rc)
		return -1;

	return __famfs_mkfs(daxdev, sb, logp, devsize, force, kill);
}
