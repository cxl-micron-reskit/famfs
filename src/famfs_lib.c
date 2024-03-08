// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

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
#include <sys/file.h>
#include <dirent.h>
#include <linux/famfs_ioctl.h>

#include "famfs_meta.h"

#include "famfs_lib.h"
#include "famfs_lib_internal.h"
#include "bitmap.h"
#include "mu_mem.h"

int mock_kmod = 0; /* unit tests can set this to avoid ioctl calls and whatnot */

struct famfs_log_stats {
	u64 n_entries;
	u64 f_logged;
	u64 f_existed;
	u64 f_created;
	u64 f_errs;
	u64 d_logged;
	u64 d_existed;
	u64 d_created;
	u64 d_errs;
};

static u8 *
famfs_build_bitmap(const struct famfs_log   *logp,
		   u64                       dev_size_in,
		   u64                      *bitmap_nbits_out,
		   u64                      *alloc_errors_out,
		   u64                      *size_total_out,
		   u64                      *alloc_total_out,
		   struct famfs_log_stats   *log_stats_out,
		   int                       verbose);
static int
famfs_dir_create(
	const char *mpt,
	const char *rpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid);

static struct famfs_superblock *famfs_map_superblock_by_path(const char *path, int read_only);
static int famfs_file_create(const char *path, mode_t mode, uid_t uid, gid_t gid,
			     int disable_write);
static int open_log_file_read_only(const char *path, size_t *sizep,
				   char *mpt_out, enum lock_opt lo);
static int famfs_mmap_superblock_and_log_raw(const char *devname,
					     struct famfs_superblock **sbp,
					     struct famfs_log **logp,
					     int read_only);

void famfs_dump_super(struct famfs_superblock *sb)
{
	int rc;

	assert(sb);
	rc = famfs_check_super(sb);
	if (rc)
		fprintf(stderr, "invalid superblock\n");

	printf("famfs superblock:\n");
	printf("\tmagic:       %llx\n", sb->ts_magic);
	printf("\tversion:     %lld\n", sb->ts_version);
	printf("\tlog offset:  %lld\n", sb->ts_log_offset);
	printf("\tlog len:     %lld\n", sb->ts_log_len);
}

void famfs_dump_log(struct famfs_log *logp)
{
	int rc;

	assert(logp);
	rc = famfs_validate_log_header(logp);
	if (rc)
		fprintf(stderr, "Error invalid log header\n");

	printf("famfs log:\n");
	printf("\tmagic:      %llx\n", logp->famfs_log_magic);
	printf("\tlen:        %lld\n", logp->famfs_log_len);
	printf("\tlast index: %lld\n", logp->famfs_log_last_index);
	printf("\tnext index: %lld\n", logp->famfs_log_next_index);
}

/**
 * famfs_module_loaded()
 *
 * This function checks whether the famfs kernel modulle is loaded
 *
 * @verbose - print informative output
 *
 * Returns: 1 if the module is loaded, and 0 if not
 */
#define FAMFS_MODULE_SYSFS "/sys/module/famfs"
int
famfs_module_loaded(int verbose)
{
	struct stat st;
	int rc;

	rc = stat(FAMFS_MODULE_SYSFS, &st);
	if (rc) {
		printf("%s: NO\n", __func__);
		return 0;
	}

	assert((st.st_mode & S_IFMT) == S_IFDIR);

	if (verbose)
		printf("%s: YES\n", __func__);
	return 1;
}

int
__file_not_famfs(int fd)
{
	int rc;

	if (mock_kmod)
		return 0;

	rc = ioctl(fd, FAMFSIOC_NOP, 0);
	if (rc)
		return 1;

	return 0;
}

int
file_not_famfs(const char *fname)
{
	int fd;
	int rc;

	fd = open(fname, O_RDONLY);
	if (fd < 0)
		return -1;

	rc = __file_not_famfs(fd);
	close(fd);
	return rc;
}

static int
file_has_map(int fd)
{
	struct famfs_ioc_map filemap = {0};
	int rc;

	rc = ioctl(fd, FAMFSIOC_MAP_GET, &filemap);
	if (rc)
		return 0; /* It's not a valid famfs file */

	return 1;
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
	if (f < 0) {
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
	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: No valid superblock\n", __func__);
		return FAMFS_NOSUPER;
	}
	if (memcmp(&my_uuid, &sb->ts_system_uuid, sizeof(my_uuid)) == 0)
		return FAMFS_MASTER;

	return FAMFS_CLIENT;
}

static int
famfs_get_role_by_dev(const char *daxdev)
{
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	int rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, &logp, 1 /* read only */);

	if (rc)
		return rc;

	rc = famfs_get_role(sb);
	munmap(sb, FAMFS_SUPERBLOCK_SIZE);
	munmap(logp, FAMFS_LOG_LEN);
	return rc;
}

static int
famfs_get_role_by_path(const char *path, uuid_le *fs_uuid_out)
{
	struct famfs_superblock *sb;
	int role;

	sb = famfs_map_superblock_by_path(path, 1 /* read only */);
	if (!sb) {
		fprintf(stderr,
			"%s: unable to find famfs superblock for path %s\n", __func__, path);
		return -1;
	}
	role = famfs_get_role(sb);
	if (fs_uuid_out)
		memcpy(fs_uuid_out, &sb->ts_uuid, sizeof(*fs_uuid_out));

	munmap(sb, FAMFS_SUPERBLOCK_SIZE);
	return role;
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
	size_t effective_log_size;
	size_t total_log_size;
	struct famfs_log_stats ls;
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
				    &fsize_sum, &alloc_sum, &ls, verbose);
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
			printf("  Device capacity:        %lld\n", dev_capacity);
			printf("  Bitmap capacity:        %lld\n", bitmap_capacity);
			printf("  Sum of file sizes:      %lld\n", fsize_sum);
			printf("  Allocated bytes:        %lld\n", alloc_sum);
			printf("  Free space:             %lld\n", bitmap_capacity - alloc_sum);
		} else {
			printf("  Device capacity:        %0.2fG\n", (float)dev_capacity / agig);
			printf("  Bitmap capacity:        %0.2fG\n", (float)bitmap_capacity / agig);
			printf("  Sum of file sizes:      %0.2fG\n", (float)fsize_sum / agig);
			printf("  Allocated space:        %.2fG\n", (float)alloc_sum / agig);
			printf("  Free space:             %.2fG\n",
			       ((float)bitmap_capacity - (float)alloc_sum) / agig);
		}
			printf("  Space amplification:     %0.2f\n", space_amp);
		printf("  Percent used:            %.1f%%\n\n", percent_used);
	}

	/* Log stats */
	printf("Famfs log:\n");
	printf("  %lld of %lld entries used\n", ls.n_entries, logp->famfs_log_last_index + 1);
	printf("  %lld files\n", ls.f_logged);
	printf("  %lld directories\n\n", ls.d_logged);

	free(bitmap);

	if (verbose) {
		printf("Verbose:\n");
		printf("  log_offset:        %lld\n", sb->ts_log_offset);
		printf("  log_len:           %lld\n", sb->ts_log_len);

		printf("  sizeof(log header) %ld\n", sizeof(struct famfs_log));
		printf("  sizeof(log_entry)  %ld\n", sizeof(struct famfs_log_entry));

		printf("  last_log_index:    %lld\n", logp->famfs_log_last_index);
		total_log_size = sizeof(struct famfs_log)
			+ (sizeof(struct famfs_log_entry) * logp->famfs_log_last_index);
		printf("  usable log size:   %ld\n", total_log_size);
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
static int
famfs_mmap_superblock_and_log_raw(const char *devname,
				  struct famfs_superblock **sbp,
				  struct famfs_log **logp,
				  int read_only)
{
	struct famfs_superblock *sb;
	int fd = 0;
	void *sb_buf = NULL;
	int rc = 0;
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	int mapmode  = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;

	fd = open(devname, openmode, 0);
	if (fd < 0) {
		if (errno == ENOENT)
			fprintf(stderr, "%s: device %s not found\n", __func__, devname);
		else
			fprintf(stderr, "%s: open %s failed; rc %d errno %d\n",
				__func__, devname, rc, errno);
		rc = -errno;
		goto err_out;
	}

	/* Map superblock and log in one call */
	sb_buf = mmap(0, FAMFS_SUPERBLOCK_SIZE + FAMFS_LOG_LEN, mapmode, MAP_SHARED, fd, 0);
	if (sb_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap superblock and log from %s\n", devname);
		rc = -1;
		goto err_out;
	}
	sb = (struct famfs_superblock *)sb_buf;
	if (sbp)
		*sbp = sb;
	if (logp)
		*logp = (struct famfs_log *)((u64)sb_buf + FAMFS_SUPERBLOCK_SIZE);

	/* Invalidate the processor cache for the superblock and log */
	invalidate_processor_cache(*logp, (*logp)->famfs_log_len);
	invalidate_processor_cache(sb, sb->ts_log_offset);

	/* TODO: using FAMFS_LOG_LEN is slightly risky, as the superblock is authoritative as
	 * to the log length. Really we should map FAMFS_SUPERBLOCK_SIZE + FAMFS_LOG_SIZE, check
	 * sb->ts_log_len and then mremap the right size if necessary. Assert for now.
	 * The smarter test is not needed until the discrepancy becomes possible.
	 */
	if (famfs_check_super(sb) == 0)
		assert((*sbp)->ts_log_len == FAMFS_LOG_LEN);

	close(fd);
	return 0;

err_out:
	if (sb_buf)
		munmap(sb_buf, FAMFS_SUPERBLOCK_SIZE);

	if (fd > 0)
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
				/* XXX Should just return xmpt - which is also malloc'd by libc */
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
 * famfs_path_is_mount_pt()
 *
 * check whether a path is a famfs mount point via /proc/mounts
 *
 * @path
 * @dev_out - if non-null, the device name will be copied here
 *
 * Return values
 * 1 - the path is an active famfs mount point
 * 0 - the path is not an active famfs mount point
 */
static int
famfs_path_is_mount_pt(const char *path, char *dev_out)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int rc;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return 0;

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
			if (strcmp(path, xmpt) == 0) {
				free(xmpt);
				free(line);
				fclose(fp);
				if (dev_out)
					strcpy(dev_out, dev);
				return 1;
			}
		}
		if (xmpt)
			free(xmpt);

	}

out:
	fclose(fp);
	if (line)
		free(line);
	return 0;
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
	filemap.extent_type    = SIMPLE_DAX_EXTENT;
	filemap.ext_list_count = nextents;

	/* TODO: check for overflow (nextents > max_extents) */
	for (i = 0; i < nextents; i++) {
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
	int role;

	dirpath[0] = 0;

	/* Get mount point path */
	mpt = famfs_get_mpt_by_dev(devname);
	if (!mpt) {
		fprintf(stderr, "%s: unable to resolve mount pt from dev %s\n", __func__, devname);
		return -1;
	}

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

	/* Check if superblock file already exists, and cleanup if bad */
	rc = stat(sb_file, &st);
	if (rc == 0) {
		if ((st.st_mode & S_IFMT) == S_IFREG) {
			/* Superblock file exists */
			if (st.st_size != FAMFS_SUPERBLOCK_SIZE) {
				fprintf(stderr,
					"%s: bad superblock file - umount/remount likely required\n",
					__func__);
				//unlink(sb_file);
			}
		} else {
			fprintf(stderr,
				"%s: non-regular file found where superblock expected\n",
				__func__);
			return -EINVAL;
		}
	}
	/* Also check if log exists and clean up if bad */

	rc = famfs_mmap_superblock_and_log_raw(devname, &sb, &logp, 1 /* Read only */);
	if (rc) {
		fprintf(stderr, "%s: superblock/log accessfailed\n", __func__);
		return -1;
	}

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock on device %s\n", __func__, devname);
		return -1;
	}

	role = famfs_get_role(sb);

	/* Create and provide mapping for Superblock file */
	sbfd = open(sb_file, O_RDWR|O_CREAT, 0444 /* sb file is read-only everywhere */);
	if (sbfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, sb_file);
		return -1;
	}

	if (file_has_map(sbfd)) {
		fprintf(stderr, "%s: found valid superblock file; doing nothing\n", __func__);
	} else {
		ext.famfs_extent_offset = 0;
		ext.famfs_extent_len    = FAMFS_SUPERBLOCK_SIZE;
		rc = famfs_file_map_create(sb_file, sbfd, FAMFS_SUPERBLOCK_SIZE, 1, &ext,
					   FAMFS_SUPERBLOCK);
		if (rc) {
			close(sbfd);
			unlink(sb_file);
			return -rc;
		}
	}
	close(sbfd);

	/* Check if log file already exists, and cleanup if bad */
	rc = stat(log_file, &st);
	if (rc == 0) {
		if ((st.st_mode & S_IFMT) == S_IFREG) {
			/* Log file exists; is it the right size? */
			if (st.st_size != sb->ts_log_len) {
				fprintf(stderr,
					"%s: bad log file - umount/mount likely required\n",
					__func__);
				//unlink(log_file);
			}
		} else {
			fprintf(stderr,
				"%s: non-regular file found where log expected\n",
				__func__);
			return -EINVAL;
		}
	}

	/* Create and provide mapping for log file
	 * Log is only writable on the master node
	 */
	logfd = open(log_file, O_RDWR|O_CREAT, (role == FAMFS_MASTER) ? 0644 : 0444);
	if (logfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, log_file);
		return -1;
	}

	if (file_has_map(sbfd)) {
		fprintf(stderr, "%s: found valid log file; doing nothing\n", __func__);
	} else {
		ext.famfs_extent_offset = sb->ts_log_offset;
		ext.famfs_extent_len    = sb->ts_log_len;
		rc = famfs_file_map_create(log_file, logfd, sb->ts_log_len, 1, &ext, FAMFS_LOG);
		if (rc)
			return -1;
	}

	close(logfd);
	printf("%s: Meta files successfully created\n", __func__);
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
famfs_mmap_whole_file(
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


/********************************************************************************
 *
 * Log play stuff
 */

static void
famfs_print_log_stats(const char *msg,
		      const struct famfs_log_stats *ls,
		      int verbose)
{
	printf("%s: processed %llu log entries; %llu new files; %llu new directories\n",
	       msg, ls->n_entries, ls->f_created, ls->d_created);
	if (verbose) {
		printf("\tCreated:  %llu files, %llu directories\n",
		       ls->f_created, ls->d_created);
		printf("\tExisted: %llu files, %llu directories\n",
		       ls->f_existed, ls->d_existed);
	}
	if (ls->f_errs || ls->d_errs)
		printf("\t%llu file errors and %llu dir errors\n",
		       ls->f_errs, ls->d_errs);
}

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

int
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
 * __famfs_logplay()
 *
 * Inner function to play the log for a famfs file system
 *
 * @logp        - pointer to a read-only copy or mmap of the log
 * @mpt         - mount point path
 * @dry_run     - process the log but don't create the files & directories
 * @client_mode - for testing; play the log as if this is a client node, even on master
 */
int
__famfs_logplay(
	const struct famfs_log *logp,
	const char             *mpt,
	int                     dry_run,
	int                     client_mode,
	int                     verbose)
{
	struct famfs_log_stats ls = { 0 };
	enum famfs_system_role role;
	struct famfs_superblock *sb;
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

	if (verbose)
		printf("famfs logplay: log contains %lld entries\n", logp->famfs_log_next_index);

	for (i = 0; i < logp->famfs_log_next_index; i++) {
		struct famfs_log_entry le = logp->entries[i];

		if (famfs_validate_log_entry(&le, i)) {
			fprintf(stderr, "%s: invalid log entry at index %lld\n", __func__, i);
			return -1;
		}
		ls.n_entries++;

		switch (le.famfs_log_entry_type) {
		case FAMFS_LOG_FILE: {
			const struct famfs_file_creation *fc = &le.famfs_fc;
			struct famfs_simple_extent *el;
			char fullpath[PATH_MAX];
			char rpath[PATH_MAX];
			struct stat st;
			int skip_file = 0;
			int fd;

			ls.f_logged++;
			if (verbose > 1)
				printf("%s: %lld file=%s size=%lld\n", __func__, i,
				       fc->famfs_relpath, fc->famfs_fc_size);

			if (!famfs_log_entry_fc_path_is_relative(fc)) {
				fprintf(stderr,
					"%s: ignoring log entry; path is not relative\n",
					__func__);
				ls.f_errs++;
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
					ls.f_errs++;
					skip_file++;
				}
			}

			if (skip_file)
				continue;

			snprintf(fullpath, PATH_MAX - 1, "%s/%s", mpt, fc->famfs_relpath);
			realpath(fullpath, rpath);
			if (dry_run)
				continue;

			rc = stat(rpath, &st);
			if (!rc) {
				if (verbose > 1)
					fprintf(stderr, "famfs logplay: File %s exists\n",
						rpath);
				ls.f_existed++;
				continue;
			}
			if (verbose) {
				printf("famfs logplay: creating file %s", fc->famfs_relpath);
				if (verbose > 1)
					printf(" mode %o", fc->fc_mode);

				printf("\n");
			}

			fd = famfs_file_create(rpath, fc->fc_mode, fc->fc_uid, fc->fc_gid,
					       (role == FAMFS_CLIENT) ? 1 : 0);
			if (fd < 0) {
				fprintf(stderr,
					"%s: unable to create destfile (%s)\n",
					__func__, fc->famfs_relpath);

				unlink(rpath);
				ls.f_errs++;
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
			ls.f_created++;
			break;
		}
		case FAMFS_LOG_MKDIR: {
			const struct famfs_mkdir *md = &le.famfs_md;
			char fullpath[PATH_MAX];
			char rpath[PATH_MAX];
			int skip_dir = 0;
			struct stat st;

			ls.d_logged++;

			if (!famfs_log_entry_md_path_is_relative(md)) {
				fprintf(stderr,
					"%s: ignoring log mkdir entry; path is not relative\n",
					__func__);
				ls.d_errs++;
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
					/* This is normal for log replay */
					if (verbose > 1) {
						fprintf(stderr,
							"famfs logplay: directory %s exists\n",
							rpath);
						ls.d_existed++;
					}
					break;

				case S_IFREG:
					fprintf(stderr,
						"%s: file (%s) exists where dir should be\n",
						__func__, rpath);
					ls.d_errs++;
					break;

				default:
					fprintf(stderr,
						"%s: something (%s) exists where dir should be\n",
						__func__, rpath);
					ls.d_errs++;
					break;
				}
				continue;
			}

			if (verbose)
				printf("famfs logplay: creating directory %s\n", md->famfs_relpath);

			rc = famfs_dir_create(mpt, (char *)md->famfs_relpath, md->fc_mode,
					      md->fc_uid, md->fc_gid);
			if (rc) {
				fprintf(stderr,
					"%s: error: unable to create directory (%s)\n",
					__func__, md->famfs_relpath);
				ls.d_errs++;
				continue;
			}

			ls.d_created++;
			break;
		}
		case FAMFS_LOG_ACCESS:
		default:
			if (verbose)
				printf("%s: invalid log entry\n", __func__);
			break;
		}
	}
	famfs_print_log_stats("famfs_logplay", &ls, verbose);

	return 0;
}

/**
 * famfs_logplay()
 *
 * Outer function to play the log for a famfs file system
 *
 * @fspath      - mount point, or any path within the famfs file system
 * @use_mmap    - Use mmap rather than reading the log into a buffer
 * @dry_run     - process the log but don't create the files & directories
 * @client_mode - for testing; play the log as if this is a client node, even on master
 * @verbose
 */
int
famfs_logplay(
	const char             *fspath,
	int                     use_mmap,
	int                     dry_run,
	int                     client_mode,
	int                     verbose)
{
	char mpt_out[PATH_MAX];
	struct famfs_log *logp;
	size_t log_size;
	int lfd;
	int rc;

	lfd = open_log_file_read_only(fspath, &log_size, mpt_out, NO_LOCK);
	if (lfd < 0) {
		fprintf(stderr, "%s: failed to open log file for filesystem %s\n",
			__func__, fspath);
		return -1;
	}

	if (use_mmap) {
		logp = mmap(0, FAMFS_LOG_LEN, PROT_READ, MAP_PRIVATE, lfd, 0);
		if (logp == MAP_FAILED) {
			fprintf(stderr, "%s: failed to mmap log file %s/.meta/log\n",
				__func__, mpt_out);
			close(lfd);
			return -1;
		}
		/* Note that this dereferences logp to get the length, and then invalidates the
		 * cache. I think this is ok...
		 */
		invalidate_processor_cache(logp, logp->famfs_log_len);
	} else {
		size_t resid = 0;
		size_t total = 0;
		char *buf;

		/* XXX: Hmm, not sure how to invalidate the processor cache before a posix read.
		 * default is mmap; posix read may not work correctly for non-cache-coherent configs
		 */
		/* Get log via posix read */
		logp = malloc(log_size);
		if (!logp) {
			close(lfd);
			fprintf(stderr, "%s: malloc %ld failed for log\n", __func__, log_size);
			return -ENOMEM;
		}
		resid = log_size;
		buf = (char *)logp;
		do {
			rc = read(lfd, &buf[total], resid);
			if (rc < 0) {
				fprintf(stderr, "%s: error %d reading log file\n",
					__func__, errno);
				return -errno;
			}
			printf("%s: read %d bytes of log\n", __func__, rc);
			resid -= rc;
			total += rc;
		} while (resid > 0);
	}
	rc = __famfs_logplay(logp, mpt_out, dry_run, client_mode, verbose);
	if (use_mmap)
		munmap(logp, FAMFS_LOG_LEN);
	else
		free(logp);
	close(lfd);
	return rc;
}

/********************************************************************************
 *
 * Log maintenance / append
 */

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
	assert(logp);
	assert(e);

	/* XXX This function is not re-entrant */

	e->famfs_log_entry_seqnum = logp->famfs_log_next_seqnum;
	e->famfs_log_entry_crc = famfs_gen_log_entry_crc(e);
	memcpy(&logp->entries[logp->famfs_log_next_index], e, sizeof(*e));

	logp->famfs_log_next_seqnum++;
	logp->famfs_log_next_index++;
	flush_processor_cache(logp, logp->famfs_log_len);
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
	assert(strlen(fullpath) >= strlen(mpt));

	if (strstr(fullpath, mpt) != fullpath) {
		/* mpt path should be a substring starting at the beginning of fullpath*/
		fprintf(stderr, "%s: failed to get relpath from mpt=%s fullpath=%s\n",
			__func__, mpt, fullpath);
		return NULL;
	}

	/* This assumes relpath() removed any duplicate '/' characters: */
	relpath = &fullpath[strlen(mpt) + 1];
	return relpath;
}

/**
 * famfs_log_file_creation()
 *
 * Returns 0 on success
 * On error, returns <0. (all failures here should abort multi-file operations)
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
		//assert(0);
		return -ENOMEM;
	}

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
/* XXX should take famfs_locked_log struct as input */
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
		//assert(0);
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
 * find_real_parent_path()
 *
 * travel up a path until a real component is found.
 * The returned path was malloc'd by realpath, and should be freed by the caller
 * This is useful in mkdir -p, where the path might be several layers deeper than
 * the deepest existing dir.
 *
 * @path
 *
 * Returns: a real sub-path, if found
 */
static char *
find_real_parent_path(const char *path)
{
	char path_copy[PATH_MAX];
	char *pc = &path_copy[0];
	int loop_ct = 64; /* This is the max depth the function can handle */
	char *rpath;

	strncpy(path_copy, path, PATH_MAX - 1);
	while (1) {
		if (strlen(pc) <= 1) {
			fprintf(stderr, "%s: path %s appears not to be in a famfs mount\n",
				__func__, path);
			return NULL;
		}

		rpath = realpath(pc, NULL);
		if (rpath)
			return rpath;  /* found a valid path */

		pc = dirname(pc);
		if (--loop_ct == 0) {
			fprintf(stderr,
				"%s: bailed from possible infinite loop; path=%s path_copy=%s\n",
				__func__, path, pc);
			return NULL;
		}
	}
	return NULL;
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
int
__open_relpath(
	const char *path,
	const char *relpath,
	int         read_only,
	size_t     *size_out,
	char       *mpt_out,
	enum lock_opt lockopt,
	int         no_fscheck)
{
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	char *rpath;
	struct stat st;
	int rc, fd;

	/*
	 * If path does not exist, ascend canonically until we find something that does
	 * exist, or until that remaining path string is too short, or until it looks like
	 * we might be in an infinite loop
	 */
	rpath = find_real_parent_path(path);
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

				if (lockopt) {
					int operation = LOCK_EX;

					if (lockopt == NON_BLOCKING_LOCK)
						operation |= LOCK_NB;
					rc = flock(fd, operation);
					if (rc) {
						fprintf(stderr, "%s: failed to get lock on %s\n",
							__func__, fullpath);
						close(fd);
						return -1;
					}
				}
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
	char       *mpt_out,
	enum lock_opt lockopt)
{
	return __open_relpath(path, LOG_FILE_RELPATH, read_only, sizep, mpt_out, lockopt, 0);
}

int
static open_log_file_read_only(
	const char *path,
	size_t     *sizep,
	char       *mpt_out,
	enum lock_opt lockopt)
{
	return __open_log_file(path, 1, sizep, mpt_out, lockopt);
}

static int
open_log_file_writable(
	const char *path,
	size_t     *sizep,
	char       *mpt_out,
	enum lock_opt lockopt)
{
	return __open_log_file(path, 0, sizep, mpt_out, lockopt);
}

static int
__open_superblock_file(
	const char *path,
	int         read_only,
	size_t     *sizep,
	char       *mpt_out)
{
	/* No need to plumb locking for the superblock; use the log for locking */
	return __open_relpath(path, SB_FILE_RELPATH, read_only, sizep, mpt_out, NO_LOCK, 0);
}

static int
open_superblock_file_read_only(
	const char *path,
	size_t     *sizep,
	char       *mpt_out)
{
	return __open_superblock_file(path, 1, sizep, mpt_out);
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
	flush_processor_cache(sb, sb_size); /* invalidate processor cache */
	return sb;
}

static struct famfs_log *
famfs_map_log_by_path(
	const char *path,
	int         read_only,
	enum lock_opt lockopt)
{
	struct famfs_log *logp;
	int prot = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;
	size_t log_size;
	void *addr;
	int fd;

	fd = __open_log_file(path, 1 /* read only */, &log_size, NULL, lockopt);
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
	flush_processor_cache(logp, log_size);  /* invalidate processor cache */
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
	 *   specified, we will find the superblock and log files and fsck the mounted
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

			logp = famfs_map_log_by_path(path, 1 /* read only */, NO_LOCK);
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

			lfd = open_log_file_read_only(path, NULL, NULL, NO_LOCK);
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

	sfd = open_superblock_file_read_only(path, &sb_size, NULL);
	if (sfd < 0)
		return sfd;

	addr = mmap(0, sb_size, PROT_READ, MAP_SHARED, sfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap superblock file\n", __func__);
		close(sfd);
		return -1;
	}
	sb = (struct famfs_superblock *)addr;
	flush_processor_cache(sb, sb_size); /* Invalidate the processor cache for the superblock */

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
 * @log_stats_out    - Optional pointer to struct log_stats to be copied out
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
		   struct famfs_log_stats   *log_stats_out,
		   int                       verbose)
{
	u64 nbits = (dev_size_in - FAMFS_SUPERBLOCK_SIZE - FAMFS_LOG_LEN) / FAMFS_ALLOC_UNIT;
	u64 bitmap_nbytes = mu_bitmap_size(nbits);
	u8 *bitmap = calloc(1, bitmap_nbytes);
	struct famfs_log_stats ls = { 0 }; /* We collect a subset of stats collected by logplay */
	u64 errors = 0;
	u64 alloc_sum = 0;
	u64 fsize_sum  = 0;
	int i, j;
	int rc;

	if (verbose > 1)
		printf("%s: dev_size %lld nbits %lld bitmap_nbytes %lld\n",
		       __func__, dev_size_in, nbits, bitmap_nbytes);

	if (!bitmap)
		return NULL;

	put_sb_log_into_bitmap(bitmap);

	if (verbose > 1) {
		printf("%s: superblock and log in bitmap:", __func__);
		mu_print_bitmap(bitmap, nbits);
	}
	/* This loop is over all log entries */
	for (i = 0; i < logp->famfs_log_next_index; i++) {
		const struct famfs_log_entry *le = &logp->entries[i];

		ls.n_entries++;

		/* TODO: validate log sequence number */

		switch (le->famfs_log_entry_type) {
		case FAMFS_LOG_FILE: {
			const struct famfs_file_creation *fc = &le->famfs_fc;
			const struct famfs_log_extent *ext = fc->famfs_ext_list;

			ls.f_logged++;
			fsize_sum += fc->famfs_fc_size;
			if (verbose > 1)
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
			ls.d_logged++;
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
	if (log_stats_out)
		memcpy(log_stats_out, &ls, sizeof(ls));
	return bitmap;
}

/**
 * bitmap_alloc_contiguous()
 *
 * @bitmap
 * @nbits - number of bits in the bitmap
 * @alloc_size - size to allocate in bytes (must convert to bits)
 *
 * Return value: the offset in bytes
 */
static s64
bitmap_alloc_contiguous(u8 *bitmap,
			u64 nbits,
			u64 alloc_size)
{
	u64 i, j;
	u64 alloc_bits = (alloc_size + FAMFS_ALLOC_UNIT - 1) /  FAMFS_ALLOC_UNIT;
	u64 bitmap_remainder;

	for (i = 0; i < nbits; i++) {
		/* Skip bits that are set... */
		if (mu_bitmap_test(bitmap, i))
			continue;

		bitmap_remainder = nbits - i;
		if (alloc_bits > bitmap_remainder) /* Remaining space is not enough */
			return -1;

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
	return -1;
}

/**
 * famfs_init_locked_log()
 *
 * @lp
 * @fspath - teh mount point full path, or any full path within a mounted famfs FS
 */
int
famfs_init_locked_log(struct famfs_locked_log *lp,
		      const char *fspath,
		      int verbose)
{
	size_t log_size;
	void *addr;
	int role;
	int rc;

	memset(lp, 0, sizeof(*lp));

	lp->devsize = famfs_validate_superblock_by_path(fspath);
	if (lp->devsize < 0)
		return -1;

	/* famfs_get_role also validates the superblock */
	role = famfs_get_role_by_path(fspath, NULL);
	if (role != FAMFS_MASTER) {
		fprintf(stderr, "%s: Error not running on FAMFS_MASTER node for this FS\n",
			__func__);
		rc = -1;
		goto err_out;
	}

	/* Log file */
	lp->lfd = open_log_file_writable(fspath, &log_size, lp->mpt, BLOCKING_LOCK);
	if (lp->lfd < 0) {
		fprintf(stderr, "%s: Unable to open famfs log for writing\n", __func__);
		/* If we can't open the log file for writing, don't allocate */
		rc = lp->lfd;
		goto err_out;
	}

	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, lp->lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		rc = -1;
		goto err_out;
	}
	lp->logp = (struct famfs_log *)addr;
	flush_processor_cache(lp->logp, log_size); /* Invalidate the processor cache for the log */
	return 0;

err_out:
	if (lp->lfd)
		close(lp->lfd);
	return rc;
}

/**
 * famfs_alloc_contiguous()
 *
 * @lp      - locked log struct. Will perform bitmap build if no already done
 * @size
 * @verbose
 */
static s64
famfs_alloc_contiguous(struct famfs_locked_log *lp, u64 size, int verbose)
{
	if (!lp->bitmap) {
		/* Bitmap is needed and hasn't been built yet */
		lp->bitmap = famfs_build_bitmap(lp->logp, lp->devsize, &lp->nbits,
						NULL, NULL, NULL, NULL, verbose);
		if (!lp->bitmap) {
			fprintf(stderr, "%s: failed to allocate bitmap\n", __func__);
			return -1;
		}
	}
	return bitmap_alloc_contiguous(lp->bitmap, lp->nbits, size);
}


static int
famfs_release_locked_log(struct famfs_locked_log *lp)
{
	int rc;

	if (lp->bitmap)
		free(lp->bitmap);

	assert(lp->lfd > 0);
	rc = flock(lp->lfd, LOCK_UN);
	if (rc)
		fprintf(stderr, "%s: unlock returned an error\n", __func__);

	close(lp->lfd);
	return rc;
}

/**
 * famfs_file_alloc()
 *
 * Alllocate space for a file, making it ready to use
 *
 * Caller has already done the following:
 * * Verify that master role via the superblock
 * * Create the stub of the new file and verify that it is in a famfs file system
 *
 * @lp       - Struct famfs_locked_log or NULL
 * @fd       - File descriptor for newly-created empty target file
 * @path     - Full path of file to allocate (needed for log entry).
 *             Caller should use realpath to get this.
 * @mode     -
 * @uid      -
 * @gid      -
 * @size     - size to alloacte
 * @verbose  -
 *
 * Returns 0 on success
 * On error, returns:
 * >0 - Errors that should not abort a multi-file operation
 * <0 - Errors that should cause an abort (such as out of space or log full)
 */
static int
famfs_file_alloc(
	struct famfs_locked_log *lp,
	int                      fd,
	const char              *path,
	mode_t                   mode,
	uid_t                    uid,
	gid_t                    gid,
	u64                      size,
	int                      verbose)
{
	struct famfs_simple_extent ext = {0};
	struct famfs_log *logp;
	char mpt[PATH_MAX];
	char *relpath;
	char *rpath = strdup(path);
	s64 offset;
	int rc = 0;

	assert(lp);
	assert(fd > 0);

	/* XXX do we need the non-locked-log case? */
	logp = lp->logp;
	strncpy(mpt, lp->mpt, PATH_MAX - 1);

	/* For the log, we need the path relative to the mount point.
	 * getting this before we allocate is cleaner if the path is sombhow bogus
	 */
	relpath = famfs_relpath_from_fullpath(mpt, rpath);
	if (!relpath)
		return -EINVAL;

	offset = famfs_alloc_contiguous(lp, size, verbose);
	if (offset < 0) {
		rc = -ENOMEM;
		fprintf(stderr, "%s: Out of space!\n", __func__);
		//assert(0);
		goto out;
	}
	/* Allocation at offset 0 is always wrong - the superblock lives there */
	assert(offset != 0);

	ext.famfs_extent_len    = round_size_to_alloc_unit(size);
	ext.famfs_extent_offset = offset;

	rc = famfs_log_file_creation(logp, 1, &ext,
				     relpath, mode, uid, gid, size);
	if (rc)
		goto out;

	if (!mock_kmod)
		rc =  famfs_file_map_create(path, fd, size, 1, &ext, FAMFS_REG);
out:
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

	/* XXX is this necessary? Have we already checked if it's in famfs? */
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
 * __famfs_mkfile()
 *
 * Inner function to create *and* allocate a file, and logs it.
 *
 * @locked_logp - We have a writable lock, which also means we're running on the master node
 * @filename
 * @mode
 * @mode
 * @uid
 * @gid
 * @size
 * @verbose
 *
 * Returns an open file descriptor if successful.
 * On failure, returns:
 *  0 - The operation failed but it's not fatal to a multi-file operationn
 * <0 - The operation failed due to a fatal condition like log full or out of space, so
 *      multi-file operations should abort
 */
int
__famfs_mkfile(
	struct famfs_locked_log *lp,
	const char              *filename,
	mode_t                   mode,
	uid_t                    uid,
	gid_t                    gid,
	size_t                   size,
	int                      verbose)
{
	//struct famfs_superblock *sb;
	char fullpath[PATH_MAX];
	int fd, rc;

	assert(lp);
	assert(size > 0);

	/* Create the file */
	fd = famfs_file_create(filename, mode, uid, gid, 0);
	if (fd <= 0)
		return fd;

	/* Clean up the filename path. (Can't call realpath until the file exists) */
	if (realpath(filename, fullpath) == NULL) {
		/* XXX this should not be possible since we created the file. hmmm... */
		fprintf(stderr, "%s: realpath() unable to rationalize filename %s\n",
			__func__, filename);
		close(fd);
		unlink(filename);
		return -EBADF;
	}

	/* If the file doesn't fit, it will be created but then unlinked
	 * (and never logged). This is probably OK
	 */
	rc = famfs_file_alloc(lp, fd, fullpath, mode, uid, gid, size, verbose);
	if (rc) {
		fprintf(stderr, "%s: famfs_file_alloc(%s, size=%ld) failed\n",
			__func__, fullpath, size);
		close(fd);
		unlink(fullpath);
		return -1;
	}
	return fd;
}

int
famfs_mkfile(
	const char       *filename,
	mode_t            mode,
	uid_t             uid,
	gid_t             gid,
	size_t            size,
	int               verbose)
{
	struct famfs_locked_log ll;
	int rc;

	if (size == 0) {
		/* We don't allow empty files; what would be the point? */
		fprintf(stderr, "%s: Creating empty file (%s) not allowed\n",
			__func__, filename);
		return -EINVAL;
	}

	rc = famfs_init_locked_log(&ll, filename, verbose);
	if (rc)
		return rc;

	rc  = __famfs_mkfile(&ll, filename, mode, uid, gid, size, verbose);

	famfs_release_locked_log(&ll);
	return rc;
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
		fprintf(stderr, "%s: failed to mkdir %s (rc %d errno %d)\n",
			__func__, fullpath, rc, errno);
		return -1;
	}

	/* Check if dir is in famfs mount? */

	if (uid && gid) {
		rc = chown(fullpath, uid, gid);
		if (rc) {
			fprintf(stderr, "%s: chown returned %d errno %d\n",
				__func__, rc, errno);
			return -1;
		}
	}
	return 0;
}

/**
 * __famfs_mkdir()
 *
 * This should become the mid-level mkdir function; verify that target is a directory
 * with a parent that exists and is in a famfs FS. Inner function should rely on these
 * checks, and use the famsf_locked_log.
 *
 * Inner function would also be callled by 'cp -r' (which doesn't exist quite yet)
 *
 * @lp         - XXX make lp mandatory?
 * @dirpath
 * @mode
 * @uid
 * @gid
 * @verbose
 */

int
__famfs_mkdir(
	struct famfs_locked_log *lp,
	const char *dirpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid,
	int         verbose)
{
	char realparent[PATH_MAX];
	char fullpath[PATH_MAX];
	char mpt_out[PATH_MAX] = { 0 };
	char realdirpath[PATH_MAX];
	char *dirdupe   = NULL;
	char *parentdir = NULL;
	char *basedupe  = NULL;
	char *newdir    = NULL;
	char *relpath   = NULL;
	struct stat st;
	int rc;

	assert(lp);

	/* Rationalize dirpath; if it exists, get role based on that */
	if (realpath(dirpath, realdirpath)) {
		/* if dirpath already exists in "non -p" mkdir, that's an error */
		return -1;
	}

	dirdupe  = strdup(dirpath);  /* call dirname() on this dupe */
	basedupe = strdup(dirpath); /* call basename() on this dupe */
	newdir   = basename(basedupe);

	/* full dirpath should not exist, but the parentdir path must exist and
	 * be a directory
	 */
	parentdir = dirname(dirdupe);
	rc = stat(parentdir, &st);
	if (rc) {
		fprintf(stderr, "%s: parent path (%s) stat failed\n", __func__, parentdir);
	} else if ((st.st_mode & S_IFMT) != S_IFDIR) {
		fprintf(stderr, "%s: parent (%s) of path %s is not a directory\n",
			__func__, dirpath, parentdir);
		rc = -1;
		goto err_out;
	}

	/* Parentdir exists and is a directory; rationalize the path with realpath */
	if (realpath(parentdir, realparent) == 0) {
		fprintf(stderr, "%s: failed to rationalize parentdir path (%s)\n",
			__func__, parentdir);
		rc = -1;
		goto err_out;
	}

	/* Rebuild full path of to-be-createed directory from the rationalized
	 * parent dir path
	 */
	rc = snprintf(fullpath, PATH_MAX - 1, "%s/%s", realparent, newdir);
	if (rc < 0) {
		fprintf(stderr, "%s: fullpath overflow\n", __func__);
		goto err_out;
	}

	strncpy(mpt_out, lp->mpt, PATH_MAX - 1);

	if (verbose)
		printf("famfs mkdir: created directory \'%s\'\n", fullpath);

	relpath = famfs_relpath_from_fullpath(mpt_out, fullpath);
	if (strcmp(mpt_out, fullpath) == 0) {
		fprintf(stderr, "%s: failed to create mount point dir: EALREADY\n", __func__);
		rc = -1;
		goto err_out;
	}
	rc = famfs_dir_create(mpt_out, relpath, mode, uid, gid);
	if (rc) {
		fprintf(stderr, "%s: failed to mkdir %s\n", __func__, fullpath);
		rc = -1;
		goto err_out;
	}

	rc = famfs_log_dir_creation(lp->logp, relpath, mode, uid, gid);


err_out:
	if (dirdupe)
		free(dirdupe);
	if (basedupe)
		free(basedupe);
	return rc;
}

int
famfs_mkdir(
	const char *dirpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid,
	int         verbose)
{
	char *cwd = get_current_dir_name();
	struct famfs_locked_log ll;
	char abspath[PATH_MAX];
	int rc;

	if (dirpath[0] == '/')
		strncpy(abspath, dirpath, PATH_MAX - 1);
	else
		snprintf(abspath, PATH_MAX - 1, "%s/%s", cwd, dirpath);

	rc = famfs_init_locked_log(&ll, abspath, verbose);
	if (rc) {
		free(cwd);
		return rc;
	}

	rc = __famfs_mkdir(&ll, dirpath, mode, uid, gid, verbose);

	famfs_release_locked_log(&ll);
	free(cwd);
	return rc;
}

/**
 * famfs_make_parent_dir()
 *
 * Recurse upwards through the path till we find a directory that exists
 * On the way back, create the missing directories for "mkdir -p"
 * If the first vallid path we find is not a directory, that's an error.
 *
 * @lp
 * @path
 * @mode
 * @uid
 * @gid
 * @depth
 */
static int
famfs_make_parent_dir(
	struct famfs_locked_log *lp,
	const char *path,
	mode_t mode,
	uid_t uid,
	gid_t gid,
	int depth,
	int verbose)
{
	char *dirdupe = strdup(path);
	char *parentdir;
	struct stat st;
	int rc;

	assert(lp);

	/* Does path already exist? */
	if (stat(path, &st) == 0) {
		free(dirdupe);
		switch (st.st_mode & S_IFMT) {
		case S_IFDIR:
			return 0;
		default:
			fprintf(stderr, "%s: path %s is not a directory\n", __func__, path);
			return -1;
		}
	}

	/* get parent path */
	parentdir = dirname(dirdupe);
	 /* Recurse up :D */
	rc = famfs_make_parent_dir(lp, parentdir, mode, uid, gid, depth + 1, verbose);
	if (rc) {
		fprintf(stderr, "%s: bad path component above (%s)\n", __func__, path);
		free(dirdupe);
		return -1;
	}

	/* Parent dir exists; now we can mkdir path! */
	free(dirdupe);
	if (verbose > 2)
		printf("%s: dir %s depth %d\n", __func__, path, depth);

	/* Parent of path is guaranteed to exist */
	rc = __famfs_mkdir(lp, path, mode, uid, gid, verbose);
	return rc;
}

int
famfs_mkdir_parents(
	const char *dirpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid,
	int         verbose)
{
	struct famfs_locked_log ll = { 0 };
	char *cwd = get_current_dir_name();
	char abspath[PATH_MAX];
	char *rpath;
	int rc;

	/* dirpath as an indeterminate number of nonexistent dirs, under a path that
	 * must exist. But the immediate parent may not exist. All existing elements
	 * in the path must be dirs. By opening the log, we can get the mount point path...
	 */

	if (dirpath[0] == '/')
		strncpy(abspath, dirpath, PATH_MAX - 1);
	else
		snprintf(abspath, PATH_MAX - 1, "%s/%s", cwd, dirpath);

	if (verbose)
		printf("%s: cwd %s abspath %s\n", __func__, cwd, abspath);

	rpath = find_real_parent_path(abspath);
	if (!rpath) {
		fprintf(stderr, "%s: failed to find real parent dir\n", __func__);
		return -1;
	}

	/* OK, we know were in a FAMFS instance. get a locked log struct */
	rc = famfs_init_locked_log(&ll, rpath, verbose);
	if (rc) {
		free(rpath);
		return rc;
	}

	/* Now recurse up fromm abspath till we find an existing parent, and mkdir back down */
	rc = famfs_make_parent_dir(&ll, abspath, mode, uid, gid, 0, verbose);

	/* Separate function should release ll and lock */
	famfs_release_locked_log(&ll);
	free(rpath);
	if (cwd)
		free(cwd);

	return rc;
}

/**
 * __famfs_cp()
 *
 * Inner file copy function
 *
 * Copy a file from any file system into famfs. A destination file is created and
 * allocated, and the data is copied info it.
 *
 * Biggest current shortcoming is that globbing and recursion is not suported.
 * Hopefully we'll get there soon.
 *
 * @lp       - famfs_locked_log struct
 * @srcfile  - must exist and be a regular file
 * @destfile - must not exist (and will be a regular file). If @destfile does not fall
 *             within a famfs file system, we will clean up and fail
 * @mode     - If mode is NULL, mode is inherited fro msource file
 * @uid
 * @gid
 * @verbose
 *
 * Return values:
 * 0  - Success
 * >0 - Something failed but if it is a multi-file copy, it should continue
 * <0 - A failure that should cause multi-file operations to bail out (such as out of space or
 *      log full...
 */
int
__famfs_cp(
	struct famfs_locked_log  *lp,
	const char               *srcfile,
	const char               *destfile,
	mode_t                    mode,
	uid_t                     uid,
	gid_t                     gid,
	int                       verbose)
{
	size_t chunksize, remainder, offset;
	int rc, srcfd, destfd;
	struct stat srcstat;
	ssize_t bytes;
	char *destp;

	assert(lp);

	/* Validate source file */
	rc = stat(srcfile, &srcstat);
	if (rc) {
		fprintf(stderr, "%s: unable to stat srcfile (%s)\n", __func__, srcfile);
		return 1; /* not an abort condition */
	}
	switch (srcstat.st_mode & S_IFMT) {
	case S_IFREG:
		/* Source is a file - all good */
		if (srcstat.st_size == 0) {
			if (verbose > 1)
				fprintf(stderr, "%s: skipping empty file %s\n", __func__, srcfile);

			return 1;
		}
		break;

	case S_IFDIR:
		/* source is a directory; fail for now
		 * (should this be mkdir? Probably... at least if it's a recursive copy)
		 */
		fprintf(stderr, "%s: -r not specified; omitting directory '%s'\n",
			__func__, srcfile);
		return 1;

	default:
		fprintf(stderr,
			"%s: error: src %s is not a regular file\n", __func__, srcfile);
		return 1;
	}

	/*
	 * Make sure we can open and read the source file
	 */
	srcfd = open(srcfile, O_RDONLY, 0);
	if (srcfd < 0) {
		fprintf(stderr, "%s: unable to open srcfile (%s)\n", __func__, srcfile);
		return 1;
	}

	/* XXX famfs_mkfile() calls famfs_file_alloc()
	 * famfs_file_alloc() allocates and logs the file under log lock
	 * but this function copies the data into the file after the log lock is released
	 * Need a way of holding the lock until the data is copied.
	 */
	destfd = __famfs_mkfile(lp, destfile, (mode == 0) ? srcstat.st_mode : mode,
				uid, gid, srcstat.st_size, verbose);
	if (destfd <= 0) {
		fprintf(stderr, "%s: failed in __famfs_mkfile\n", __func__);
		return destfd;
	}

	destp = mmap(0, srcstat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, destfd, 0);
	if (destp == MAP_FAILED) {
		fprintf(stderr, "%s: dest mmap failed (%s) size %ld\n",
			__func__, destfile, srcstat.st_size);
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
			munmap(destp, srcstat.st_size);
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
	/* Flush the processor cache for the dest file */
	flush_processor_cache(destp, srcstat.st_size);

	munmap(destp, srcstat.st_size);
	close(srcfd);
	close(destfd);
	return 0;
}

/**
 * famfs_cp()
 *
 * Mid layer file copy function
 *
 * @lp       - Locked Log struct (required)
 * @srcfile  - skipped unless it's a regular file
 * @destfile - Nonexistent file, or existing directory. If destfile is a directory, this
 *             function fill append basename(srcfile) to destfile to get a nonexistent file path
 * @verbose
 */
static int
famfs_cp(struct famfs_locked_log *lp,
	 const char              *srcfile,
	 const char              *destfile,
	 mode_t                   mode,
	 uid_t                    uid,
	 gid_t                    gid,
	 int                      verbose)
{
	char actual_destfile[PATH_MAX] = { 0 };
	struct stat deststat;
	int rc;

	assert(lp);

	/* Figure out what the destination is; possibilities:
	 *
	 * * A non-existing path whose parent directory is in famfs
	 * * An existing path do a directory in famfs
	 */
	rc = stat(destfile, &deststat);
	if (!rc) {
		switch (deststat.st_mode & S_IFMT) {
		case S_IFDIR: {
			char destpath[PATH_MAX];
			char src[PATH_MAX];

			if (verbose > 1)
				printf("%s: (%s) -> (%s/)\n", __func__, srcfile, destfile);

			/* Destination is directory;  get the realpath and append the basename
			 * from the source
			 */
			if (realpath(destfile, destpath) == 0) {
				fprintf(stderr, "%s: failed to rationalize destath path (%s)\n",
					__func__, destfile);
				return 1;
			}
			strncpy(src, srcfile, PATH_MAX - 1);
			snprintf(destpath, PATH_MAX - 1, "%s/%s", destfile, basename(src));
			strncpy(actual_destfile, destpath, PATH_MAX - 1);
			break;
		}
		default:
			fprintf(stderr,
				"%s: error: destination file (%s) exists and is not a directory\n",
				__func__, destfile);
			return -EEXIST;
		}
	} else {
		/* File does not exist;
		 * the check whether it is in famfs will happen after the file is created
		 */
		if (verbose > 1)
			printf("%s: (%s) -> (%s)\n", __func__, srcfile, destfile);

		strncpy(actual_destfile, destfile, PATH_MAX - 1);
	}

	return __famfs_cp(lp, srcfile, actual_destfile, mode, uid, gid, verbose);
}

/**
 * famfs_cp_dir()
 *
 * Copy a directory and its contents to a target path
 *
 * @lp       - (required)
 * @src      - src path (must exist and be a directory)
 * @dest     - Must be a directory if it exists; if dest does not exist, its parent dir
 *             is required to exist.
 * @mode
 * @uid
 * @gid
 * @verbose
 */
int famfs_cp_dir(
	struct famfs_locked_log *lp,
	const char *src,
	const char *dest,
	mode_t mode,
	uid_t uid,
	gid_t gid,
	int verbose)
{
	struct dirent *entry;
	DIR *directory;
	struct stat st;
	int rc;
	int err = 0;

	assert(lp);
	if (verbose > 1)
		printf("%s: (%s) -> (%s)\n", __func__, src, dest);

	/* Does the dest dir exist? */
	rc = stat(dest, &st);
	if (rc) {
		/* The directory doesn't exist yet */
		rc = __famfs_mkdir(lp, dest, mode, uid, gid, verbose);
		if (rc) {
			/* Recursive copy can't really recover from a mkdir failure */
			return rc;
		}
	}

	directory = opendir(src);
	if (directory == NULL) {
		/* XXX is it possible to get here since we created the dir if it didn't exist? */
		fprintf(stderr, "%s: failed to open src dir (%s)\n", __func__, src);
		return 1;
	}

	/* Loop through the directry entries */
	while ((entry = readdir(directory)) != NULL) {
		char srcfullpath[PATH_MAX];
		struct stat src_stat;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(srcfullpath, PATH_MAX - 1, "%s/%s", src, entry->d_name);
		rc = stat(srcfullpath, &src_stat);
		if (rc) {
			fprintf(stderr, "%s: failed to stat source path (%s)\n",
				__func__, srcfullpath);
			err = 1;
			continue;
		}

		if (verbose)
			printf("famfs cp:  %s/%s\n", dest, entry->d_name);

		switch (src_stat.st_mode & S_IFMT) {
		case S_IFREG:
			rc = famfs_cp(lp, srcfullpath, dest, mode, uid, gid, verbose);
			if (rc < 0) {
				err = rc;
				goto bailout;
			}
			if (rc)
				err = 1; /* if anything failed, return 1 */
			break;

		case S_IFDIR: {
			char newdirpath[PATH_MAX];
			char *src_copy = strdup(srcfullpath);

			snprintf(newdirpath, PATH_MAX - 1, "%s/%s", dest, basename(src_copy));
			/* Recurse :D
			 * Parent of newdirpath is guaranteed to exist, because that's a property
			 * of this recursion
			 */
			rc = famfs_cp_dir(lp, srcfullpath, newdirpath, mode, uid, gid, verbose);
			free(src_copy);
			break;
		}
		default:
			fprintf(stderr,
				"%s: error: skipping non-file or directory %s\n",
				__func__, srcfullpath);
			return -EINVAL;
		}
	}
bailout:
	closedir(directory);
	return err;
}

/**
 * famfs_cp_multi()
 *
 * Copy multiple files from anywhere to famfs
 *
 * @argc    - number of args
 * @argv    - array of args
 * @mode
 * @uid
 * @gid
 * @recursive - Recursive copy if true
 * @verbose -
 *
 * Rules:
 * * non-recuraive
 *   * If there are more than 2 args, last arg must be a directory
 *   * In the 2 arg case, last arg can be either a directory or a non-existent file name
 *   * Files will be copied to their basename in the last-arg directory
 *   * Any directories before the last arg will skipped (until we have 'cp -r' implemented
 *   * Everything that can be copied according to these rules will be copied (but the return
 *     value will be 1 if anything failed
 *
 * * Recursive
 *   * Last arg must be a directory which need not already exist
 *   * Directories and their contents will be recursively copied
 *
 * Return value:
 * * 0 if everything succeeded
 * * non-zero if anything failed
 */

int
famfs_cp_multi(
	int argc,
	char *argv[],
	mode_t mode,
	uid_t uid,
	gid_t gid,
	int recursive,
	int verbose)
{
	struct famfs_locked_log ll = { 0 };
	char *dest = argv[argc - 1];
	int src_argc = argc - 1;
	char *dirdupe   = NULL;
	char *parentdir = NULL;
	struct stat st;
	char *dest_parent_path;
	int err = 0;
	int rc;
	int i;

	/* Parent directory of destination must exist */
	dirdupe = strdup(dest);
	parentdir = dirname(dirdupe);
	dest_parent_path = realpath(parentdir, NULL);
	if (!dest_parent_path) {
		free(dirdupe);
		fprintf(stderr, "%s: unable to get realpath for (%s)\n", __func__, dest);
		return -1;
	}

	/* Check to see if the parent of the destination (last arg) is a directory.
	 * if not, error out
	 */
	rc = stat(dest_parent_path, &st);
	if (!rc) {
		switch (st.st_mode & S_IFMT) {
		case S_IFDIR:
			/* It's a directory - all good */
			break;
		default:
			fprintf(stderr,
				"%s: Error: dest parent (%s) exists and is not a directory\n",
				__func__, dest_parent_path);
			free(dest_parent_path);
			free(dirdupe);
			return -1;
		}
	}

	/* If this is a recursive request, or if argc > 2, the destination must be a directory,
	 * although it need not exist yet. But if the destination exists and is not a dir,
	 * that's an error
	 */
	if (recursive || (argc > 2)) {
		rc = stat(dest, &st);
		if (!rc) {
			switch (st.st_mode & S_IFMT) {
			case S_IFDIR:
				/* It's a directory - all good */
				break;
			default:
				fprintf(stderr,
					"%s: Error: destination (%s) exists and is not a directory\n",
					__func__, dest_parent_path);
				free(dest_parent_path);
				free(dirdupe);
				return -1;
			}
		}
	}

	rc = famfs_init_locked_log(&ll, dest_parent_path, verbose);
	if (rc) {
		free(dest_parent_path);
		free(dirdupe);
		return rc;
	}

	for (i = 0; i < src_argc; i++) {
		struct stat src_stat;

		/* Need to handle source files and directries differently */
		rc = stat(argv[i], &src_stat);
		if (verbose)
			printf("%s:  %s\n", __func__, argv[i]);

		switch (src_stat.st_mode & S_IFMT) {
		case S_IFREG:
			/* Dest is a directory and files will be copied into it */
			rc = famfs_cp(&ll, argv[i], dest, mode, uid, gid, verbose);
			if (rc < 0) { /* rc < 0 is errors we abort after */
				fprintf(stderr, "%s: aborting copy due to error\n",
					__func__);
				err = rc;
				goto err_out;
			}
			if (rc) /* rc > 0 is errors that we continue after */
				err = 1; /* if anything failed, return 1 */
			break;

		case S_IFDIR:
			if (recursive) {
				/* Parent is guaranteed to exist, we verified it above */
				rc = famfs_cp_dir(&ll, argv[i], dest, mode, uid,
						  gid, verbose);
				if (rc < 0) { /* rc < 0 is errors we abort after */
					fprintf(stderr, "%s: aborting copy due to error\n",
						__func__);
					err = rc;
					goto err_out;
				}
				if (rc)  /* rc > 0 is errors that we continue after */
					err = 1;
			} else {
				fprintf(stderr, "%s: -r not specified; omitting directory '%s'\n",
					__func__, argv[i]);
				err = 1;
			}
			break;
		default:
			fprintf(stderr,
				"%s: error: skipping non-file or directory %s\n",
				__func__, argv[i]);
			err = -EINVAL;
			goto err_out;
		}

		/* cp continues even if some files were not copied */
	}

err_out:
	/* Separate function should release ll and lock */
	free(dirdupe);
	famfs_release_locked_log(&ll);
	free(dest_parent_path);
	return err;
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
	int src_role, dest_role;
	uuid_le src_fs_uuid, dest_fs_uuid;
	struct stat src_stat;
	int rc;

	/* srcfile must already exist; Go ahead and check that first */
	if (realpath(srcfile, srcfullpath) == NULL) {
		fprintf(stderr, "%s: bad source path %s\n", __func__, srcfile);
		return -1;
	}
	/* and srcfile must be in famfs... */
	if (file_not_famfs(srcfullpath)) {
		fprintf(stderr, "%s: source path (%s) not in a famfs file system\n",
			__func__, srcfullpath);
		return -1;
	}
	rc = stat(srcfullpath, &src_stat);
	if (rc < 0) {
		fprintf(stderr, "%s: unable to stat srcfile %s\n", __func__, srcfullpath);
		return -1;
	}

	/*
	 * Need to confirm that both files are inn the same file system. Otherwise,
	 * the cloned extents will be double-invalid on the second file :(
	 */
	src_role = famfs_get_role_by_path(srcfile, &src_fs_uuid);
	dest_role = famfs_get_role_by_path(destfile, &dest_fs_uuid);
	if (src_role < 0) {
		fprintf(stderr, "%s: Error: unable to check role for src file %s\n",
			__func__, srcfullpath);
		return -1;
	}
	if (dest_role < 0) {
		fprintf(stderr, "%s: Error: unable to check role for src file %s\n",
			__func__, destfile);
		return -1;
	}
	if ((src_role != dest_role) ||
	    memcmp(&src_fs_uuid, &dest_fs_uuid, sizeof(src_fs_uuid)) != 0) {
		fprintf(stderr,
			"%s: Error: source and destination must be in the same file system\n",
			__func__);
		return -1;
	}
	if (src_role != FAMFS_MASTER) {
		fprintf(stderr, "%s: file creation not allowed on client systems\n", __func__);
		return -EPERM;
	}
	/* FAMFS_MASTER role now confirmed, and the src and destination are in the same famfs */

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
	lfd = open_log_file_writable(srcfullpath, &log_size, mpt_out, BLOCKING_LOCK);
	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		rc = -1;
		goto err_out;
	}
	logp = (struct famfs_log *)addr;

	/* Clone is only allowed on the master, so we don't need to invalidate the cache */

	/* Create the destination file. This will be unlinked later if we don't get all
	 * the way through the operation.
	 */
	dfd = famfs_file_create(destfile, src_stat.st_mode, src_stat.st_uid, src_stat.st_gid, 0);
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

	rc = famfs_log_file_creation(logp, filemap.ext_list_count, se,
				     relpath, src_stat.st_mode, src_stat.st_uid, src_stat.st_gid,
				     filemap.file_size);
	if (rc) {
		fprintf(stderr,
			"%s: failed to log caller-specified allocation\n",
			__func__);
		rc = -1;
		unlink(destfullpath);
		goto err_out;
	}

	close(lfd); /* Closing releases the lock */
	lfd = 0;
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
		flush_processor_cache(sb, sb->ts_log_offset);
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

	/* Force a writeback of the log followed by the superblock */
	flush_processor_cache(logp, logp->famfs_log_len);
	flush_processor_cache(sb, sb->ts_log_offset);
	return 0;
}

int
famfs_mkfs(const char *daxdev,
	   int         kill,
	   int         force)
{
	int rc;
	size_t devsize;
	enum extent_type type = SIMPLE_DAX_EXTENT;
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	u64 min_devsize = 4 * 1024ll * 1024ll * 1024ll;

	rc = famfs_get_role_by_dev(daxdev);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to establish role\n", __func__);
		return rc;
	}

	/* If the role is FAMFS_CLIENT, there is a superblock already;
	 * if the role is not FAMFS_CLIENT, its' either FAMFS_MASTER OR FAMFS_NOSUPER;
	 * In either of those cases it's ok to mkfs.
	 *
	 * If the role is FAMFS_CLIENT, they'll have to manually blow away the superblock
	 * if they want to do a new mkfs.
	 */
	if (rc == FAMFS_CLIENT)
		return rc;

	rc = famfs_get_device_size(daxdev, &devsize, &type);
	if (rc)
		return -1;

	printf("devsize: %ld\n", devsize);

	if (devsize < min_devsize) {
		fprintf(stderr, "%s: unsupported memory device size (<4GiB)\n", __func__);
		return -EINVAL;
	}

	/* XXX Get role first via read-only sb. If daxdev contains a fs that was not
	 * created on this host, fail unless force is specified
	 */

	rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, &logp, 0 /* read/write */);
	if (rc)
		return -1;

	return __famfs_mkfs(daxdev, sb, logp, devsize, force, kill);
}

int
famfs_recursive_check(const char *dirpath,
		      u64 *nfiles_out,
		      u64 *ndirs_out,
		      u64 *nerrs_out,
		      int verbose)
{
	struct dirent *entry;
	DIR *directory;
	struct stat st;
	u64 nfiles = 0;
	u64 ndirs = 0;
	int nerrs = 0;
	int rc;

	directory = opendir(dirpath);
	if (directory == NULL) {
		/* XXX is it possible to get here since we created the dir if it didn't exist? */
		fprintf(stderr, "%s: failed to open src dir (%s)\n", __func__, dirpath);
		return -1;
	}

	/* Loop through the directry entries */
	while ((entry = readdir(directory)) != NULL) {
		char fullpath[PATH_MAX];
		struct famfs_ioc_map filemap = {0};
		int fd;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(fullpath, PATH_MAX - 1, "%s/%s", dirpath, entry->d_name);
		rc = stat(fullpath, &st);
		if (rc) {
			fprintf(stderr, "%s: failed to stat source path (%s)\n",
				__func__, fullpath);
			nerrs++;
			continue;
		}

		if (verbose)
			printf("%s:  %s\n", __func__, fullpath);

		switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			nfiles++;
			fd = open(fullpath, O_RDONLY, 0);
			if (fd <= 0) {
				fprintf(stderr, "%s: failed to open file %s\n",
					__func__, fullpath);
				//nerrs++;
				continue;
			}
			rc = ioctl(fd, FAMFSIOC_MAP_GET, &filemap);
			if (rc) {
				fprintf(stderr, "%s: Error file not mapped: %s\n",
					__func__, fullpath);
				nerrs++;
			}
			close(fd);
			break;

		case S_IFDIR: {
			u64 nfiles_out = 0;
			u64 ndirs_out = 0;
			u64 nerrs_out = 0;

			ndirs++;
			/* Recurse :D */
			rc = famfs_recursive_check(fullpath, &nfiles_out, &ndirs_out,
						   &nerrs_out, verbose);
			nfiles += nfiles_out;
			ndirs += ndirs_out;
			nerrs += nerrs_out;
			break;
		}
		default:
			if (verbose)
				fprintf(stderr,
					"%s: skipping non-file or directory %s\n",
					__func__, fullpath);
		}
	}

	closedir(directory);
	if (nfiles_out)
		*nfiles_out = nfiles;
	if (ndirs_out)
		*ndirs_out = ndirs;
	if (nerrs_out)
		*nerrs_out = nerrs;
	rc = (nerrs) ? 1 : 0;
	return rc;
}

int
famfs_check(const char *path,
	    int verbose)
{
	char metadir[PATH_MAX];
	char logpath[PATH_MAX];
	char dev_out[PATH_MAX];
	char sbpath[PATH_MAX];
	struct stat st;
	u64 nfiles_out = 0;
	u64 ndirs_out = 0;
	u64 nerrs_out = 0;
	u64 nfiles = 0;
	u64 ndirs = 0;
	u64 nerrs = 0;
	int rc;

	if (path[0] != '/') {
		fprintf(stderr, "%s: must use absolute path of mount point\n", __func__);
		return -1;
	}
	if (!famfs_path_is_mount_pt(path, dev_out)) {
		fprintf(stderr, "%s: path (%s) is not a famfs mount point\n", __func__, path);
		return -1;
	}

	snprintf(metadir, PATH_MAX - 1, "%s/.meta", path);
	snprintf(sbpath, PATH_MAX - 1, "%s/.meta/.superblock", path);
	snprintf(logpath, PATH_MAX - 1, "%s/.meta/.log", path);
	rc = stat(metadir, &st);
	if (rc) {
		fprintf(stderr, "%s: Need to run mkmeta on device %s for this file system\n",
			__func__, dev_out);
		ndirs++;
		return -1;
	}
	rc = stat(sbpath, &st);
	if (rc) {
		fprintf(stderr, "%s: superblock file not found for file system %s\n",
			__func__, path);
		nerrs++;
	}

	rc = stat(logpath, &st);
	if (rc) {
		fprintf(stderr, "%s: log file not found for file system %s\n",
			__func__, path);
		nerrs++;
	}

	rc = famfs_recursive_check(path, &nfiles_out, &ndirs_out, &nerrs_out, verbose);
	nfiles += nfiles_out;
	ndirs += ndirs_out;
	nerrs += nerrs_out;
	printf("%s: %lld files, %lld directories, %lld errors\n", __func__, nfiles, ndirs, nerrs);
	return rc;
}
