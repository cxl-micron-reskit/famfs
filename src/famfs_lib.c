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
#include <sys/statfs.h>
#include <linux/famfs_ioctl.h>
#include <linux/magic.h>
#include <pthread.h>

#include "famfs_meta.h"
#include "famfs_lib.h"
#include "famfs_lib_internal.h"
#include "mu_mem.h"
#include "thpool.h"

int mock_kmod = 0; /* unit tests can set this to avoid ioctl calls and whatnot */
int mock_fstype = 0;
int mock_flush = 0; /* for unit tests to avoid actual flushing */
int mock_role = 0; /* for unit tests to specify role rather than testing for it */
int mock_uuid = 0; /* for unit tests to simulate uuid related errors */
int mock_path = 0; /* for unit tests to simulate path related errors */
int mock_failure = 0; /* for unit tests to simulate a failure case */
int mock_stripe = 0; /* relaxes stripe rules for unit tests */
int mock_threadpool = 0; /* call threaded code rather than threading */

static int
famfs_dir_create(
	const char *mpt,
	const char *rpath,
	mode_t      mode,
	uid_t       uid,
	gid_t       gid);

static struct famfs_superblock *famfs_map_superblock_by_path(const char *path, int read_only);
static int famfs_file_create_stub(const char *path, mode_t mode, uid_t uid, gid_t gid,
			     int disable_write);
static int famfs_shadow_file_create(const char *path,
				    const struct famfs_log_file_meta *fc,
				    struct famfs_log_stats *ls, int disable_write, int dry_run,
				    int testmode, int verbose);
static int open_log_file_read_only(const char *path, size_t *sizep,
				   ssize_t size_in,
				   char *mpt_out, enum lock_opt lo);
static int famfs_mmap_superblock_and_log_raw(const char *devname,
					     struct famfs_superblock **sbp,
					     struct famfs_log **logp,
					     u64 log_len,
					     int read_only);
static int open_superblock_file_read_only(const char *path, size_t  *sizep, char *mpt_out);
static char *famfs_relpath_from_fullpath(const char *mpt, char *fullpath);

/* famfs v2 stuff (dual standalone / fuse) */

static const char *
famfs_mount_type(int type)
{
	if (type < 0)
		return "invalid";

	switch (type) {
	case FAMFS_V1:
		return "FAMFS_V1";
	case FAMFS_FUSE:
		return "FAMFS_FUSE";
	case NOT_FAMFS:
		return "NOT_FAMFS";
	default:
		return "INVALID";
	}
}

int
file_is_famfs(const char *fname)
{
	struct statfs fs;

	if (mock_fstype)
		return mock_fstype;
	if (statfs(fname, &fs)) {
		char *local_path = strdup(fname);
		local_path = dirname(local_path);
		if (statfs(local_path, &fs)) {
			fprintf(stderr,
				"%s: statfs failed for path %s and its parent\n",
				__func__, fname);
			free(local_path);
			return -1; /* fname not found */
		}
		free(local_path);
	}

	switch (fs.f_type) {
	case FAMFS_SUPER_MAGIC: /* deprecated but older v1 returns this */
		return FAMFS_V1;

	case FAMFS_STATFS_MAGIC_V1:
		return FAMFS_V1;

	case FUSE_SUPER_MAGIC:  /* accept fuse magic until it returns ouer own */
		return FAMFS_FUSE;

	case FAMFS_STATFS_MAGIC:
		return FAMFS_FUSE;
	}
	/* Not famfs */
	return NOT_FAMFS;
}

/* end famfs v2 */

/**
 * famfs_file_read()
 *
 * Read from a file, handling short reads
 *
 * Return values:
 *  0 - success (the correct amount of data was read
 * !0 - failure (the wrong amount of data, including NONE, was read)
 */
static ssize_t
famfs_file_read(
	int fd,
	char *buf,
	ssize_t size,
	const char *func,
	const char *msg,
	int verbose)
{
	size_t resid = size;
	size_t total = 0;
	int rc;

	assert(fd > 0);

	do {
		rc = read(fd, &buf[total], resid);
		if (rc < 0) {
			fprintf(stderr, "%s: error %d reading %s\n",
				func, errno, msg);
			return -errno;
		}
		if (verbose)
			printf("%s: read %d bytes from %s\n", func, rc, msg);

		if (rc == 0)
			return -1; /* partial read is an error */
		resid -= rc;
		total += rc;
	} while (resid > 0);
	return 0;
}

/**
 * famfs_module_loaded()
 *
 * This function checks whether the famfs kernel modulle is loaded
 *
 * Returns: 1 if the module is loaded, and 0 if not
 */
#define FAMFS_MODULE_SYSFS   "/sys/module/famfs"
#define FAMFS_MODULE_SYSFSV1 "/sys/module/famfsv1"
int
famfs_module_loaded(int verbose)
{
	struct stat st;
	int rc;

	rc = stat(FAMFS_MODULE_SYSFS, &st);
	if (rc) {
		rc = stat(FAMFS_MODULE_SYSFSV1, &st);
		if (rc) {
			printf("%s: NO\n", __func__);
			return 0;
		}
	}

	assert((st.st_mode & S_IFMT) == S_IFDIR);

	if (verbose)
		printf("%s: YES\n", __func__);
	return 1;
}

int
__file_is_famfs_v1(int fd)
{
	int rc;

	if (mock_kmod)
		return 1;

	rc = ioctl(fd, FAMFSIOC_NOP, 0);
	if (rc)
		return 0;

	return 1;
}

int
file_is_famfs_v1(const char *fname)
{
	int fd;
	int rc;

	fd = open(fname, O_RDONLY);
	if (fd < 0)
		return 0;

	rc = __file_is_famfs_v1(fd);
	close(fd);
	return rc;
}

static int
file_has_v1_map(int fd)
{
	struct famfs_ioc_map filemap = {0};
	int rc;

	rc = ioctl(fd, FAMFSIOC_MAP_GET, &filemap);
	if (rc)
		return 0; /* It's not a valid famfs file */

	return 1;
}

void
famfs_print_role_string(int role)
{
	switch (role) {
	case FAMFS_MASTER:
		printf("Owner\n");
		return;
	case FAMFS_CLIENT:
		printf("Client\n");
		return;
	case FAMFS_NOSUPER:
		printf("Invalid superblock\n");
		return;
	}
}

/**
 * famfs_get_role()
 *
 * Check whether this host is the master or not. If not the master, it must not attempt
 * to write the superblock or log, and files will default to read-only
 */
static enum famfs_system_role
famfs_get_role(const struct famfs_superblock *sb)
{
	uuid_le my_uuid;
	int rc;

	if (mock_role)
		return mock_role;

	rc = famfs_get_system_uuid(&my_uuid);
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

int
famfs_get_role_by_dev(const char *daxdev)
{
	struct famfs_superblock *sb;
	int rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, NULL,
						   0, 1 /* read only */);

	if (rc)
		return rc;

	rc = famfs_get_role(sb);
	munmap(sb, FAMFS_SUPERBLOCK_SIZE);

	return rc;
}

static int
famfs_get_role_by_path(
	const char *path,
	uuid_le *fs_uuid_out)
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
famfs_get_device_size(
	const char       *fname,
	size_t           *size,
	enum famfs_extent_type *type)
{
	char spath[PATH_MAX];
	//char *base_name;
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

	//base_name = strrchr(fname, '/');
	switch (st.st_mode & S_IFMT) {
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

	crc = crc32(crc, (const unsigned char *)&sb->ts_alloc_unit, sizeof(sb->ts_alloc_unit));
	crc = crc32(crc, (const unsigned char *)&sb->ts_omf_ver_major,
		    sizeof(sb->ts_omf_ver_major));
	crc = crc32(crc, (const unsigned char *)&sb->ts_omf_ver_minor,
		    sizeof(sb->ts_omf_ver_minor));

	crc = crc32(crc, (const unsigned char *)&sb->ts_uuid,        sizeof(sb->ts_uuid));
	crc = crc32(crc, (const unsigned char *)&sb->ts_dev_uuid,    sizeof(sb->ts_uuid));
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
	struct famfs_log_stats ls;
	u64 alloc_sum, fsize_sum;
	size_t total_log_size;
	u64 dev_capacity;
	u64 alloc_unit;
	u64 errors = 0;
	u8 *bitmap;
	u64 nbits;
	int role;

	assert(sb);
	assert(logp);

	alloc_unit = sb->ts_alloc_unit;
	assert(alloc_unit == 4096 || alloc_unit == 0x200000);
	dev_capacity = sb->ts_daxdev.dd_size;
	effective_log_size = sizeof(*logp) +
		(logp->famfs_log_next_index * sizeof(struct famfs_log_entry));

	/*
	 * Print superblock info
	 */
	printf("Famfs Superblock:\n");
	printf("  Filesystem UUID:   ");
	famfs_print_uuid(&sb->ts_uuid);
	printf("  Device UUID:       ");
	famfs_print_uuid(&sb->ts_dev_uuid);
	printf("  System UUID:       ");
	famfs_print_uuid(&sb->ts_system_uuid);
	printf("  role of this node: ");
	role = famfs_get_role(sb);
	famfs_print_role_string(role);
	printf("  alloc_unit:        0x%llx\n", sb->ts_alloc_unit);
	printf("  OMF major version: %d\n", sb->ts_omf_ver_major);
	printf("  OMF minor version: %d\n", sb->ts_omf_ver_minor);

	printf("  sizeof superblock: %ld\n", sizeof(struct famfs_superblock));
	printf("  log size (bytes):  %lld\n", sb->ts_log_len);
	printf("  primary: %s   %ld\n",
	       sb->ts_daxdev.dd_daxdev, sb->ts_daxdev.dd_size);

	/*
	 * print log info
	 */
	printf("\nLog stats:\n");
	printf("  # of log entries in use: %lld of %lld\n",
	       logp->famfs_log_next_index, logp->famfs_log_last_index + 1);
	printf("  Log size in use:          %ld\n", effective_log_size);
	printf("  Log size (total bytes)    %lld\n", logp->famfs_log_len);

	/*
	 * Build the log bitmap to scan for errors
	 */
	bitmap = famfs_build_bitmap(logp, alloc_unit, dev_capacity,
				    &nbits, &errors,
				    &fsize_sum, &alloc_sum, &ls, verbose);
	if (errors)
		printf("ERROR: %lld ALLOCATION COLLISIONS FOUND\n", errors);
	else {
		u64 bitmap_capacity = nbits * alloc_unit;
		float space_amp = (float)alloc_sum / (float)fsize_sum;
		float percent_used = 100.0 * ((float)alloc_sum /
					      (float)bitmap_capacity);
		float agig = 1024 * 1024 * 1024;

		printf("  No allocation errors found\n\n");
		printf("Capacity:\n");
		if (!human) {
			printf("  Device capacity:        %lld\n",
			       dev_capacity);
			printf("  Bitmap capacity:        %lld\n",
			       bitmap_capacity);
			printf("  Sum of file sizes:      %lld\n", fsize_sum);
			printf("  Allocated bytes:        %lld\n", alloc_sum);
			printf("  Free space:             %lld\n",
			       bitmap_capacity - alloc_sum);
		} else {
			printf("  Device capacity:        %0.2fG\n",
			       (float)dev_capacity / agig);
			printf("  Bitmap capacity:        %0.2fG\n",
			       (float)bitmap_capacity / agig);
			printf("  Sum of file sizes:      %0.2fG\n",
			       (float)fsize_sum / agig);
			printf("  Allocated space:        %.2fG\n",
			       (float)alloc_sum / agig);
			printf("  Free space:             %.2fG\n",
			       ((float)bitmap_capacity - (float)alloc_sum) / agig);
		}
		printf("  Space amplification:     %0.2f\n", space_amp);
		printf("  Percent used:            %.1f%%\n\n", percent_used);
	}

	/* Log stats */
	printf("Famfs log:\n");
	printf("  %lld of %lld entries used\n",
	       ls.n_entries, logp->famfs_log_last_index + 1);
	printf("  %lld bad log entries detected\n", ls.bad_entries);
	printf("  %lld files\n", ls.f_logged);
	printf("  %lld directories\n\n", ls.d_logged);

	free(bitmap);

	if (verbose) {
		printf("Verbose:\n");
		printf("  log_offset:        %lld\n", sb->ts_log_offset);
		printf("  log_len:           %lld\n", sb->ts_log_len);

		printf("  log_entry components:\n");
		printf("      sizeof(log header) %ld\n",
		       sizeof(struct famfs_log));
		printf("      sizeof(log_entry)  %ld\n",
		       sizeof(struct famfs_log_entry));
		printf("          sizeof(mkdir)     %ld\n",
		       sizeof(struct famfs_log_mkdir));
		printf("          sizeof(file_meta) %ld\n",
		       sizeof(struct famfs_log_file_meta));
		printf("              sizeof(fmap)    %ld\n",
		       sizeof(struct famfs_log_fmap));

		printf("                  sizeof(interleaved_ext[%d]): %ld\n",
		       FAMFS_MAX_INTERLEAVED_EXTENTS,
		       sizeof(struct famfs_interleaved_ext));
		printf("                  sizeof(famfs_simple_ext[%d]): %ld\n",
		       FAMFS_MAX_SIMPLE_EXTENTS,
		       sizeof(struct famfs_simple_extent));

		printf("  last_log_index:    %lld\n",
		       logp->famfs_log_last_index);
		total_log_size = sizeof(struct famfs_log)
			+ (sizeof(struct famfs_log_entry) *
			   logp->famfs_log_last_index);
		printf("  usable log size:   %ld\n", total_log_size);
		printf("  sizeof(struct famfs_log_file_meta): %ld\n",
		       sizeof(struct famfs_log_file_meta));
		printf("\n");
	}
	return errors;
}

/**
 * famfs_mmap_superblock_and_log_raw()
 *
 * This function mmaps a superblock and log directly from a dax device.
 * This is used during mkfs, mount, and fsck (only if the file system is not
 * mounted). No other apps should use this interface.
 *
 * If called with a NULL @logp, only the superblock is mapped (which is
 * deterministically sized at FAMFS_SUPERBLOCK_SIZE.
 *
 * If @logp is non-NULL, we attempt to map the log as follows:
 * * If log_size==0, we figure out the log size and map it - if there is a
 *   valid superblock
 * * If log_size>0 (and is a multiple of 2MiB), we attempt to map log_size
 *   from offset FAMFS_LOG_OFFSET into the device.
 *
 * The superblock is not validated - UNLESS we need to get the log size from it,
 * in which case we must validate the superblock.
 *
 * @devname:   dax device name
 * @sbp:       (mandatory) Return superblock in this pointer
 * @logp:      (optional)  Map log and return it in this pointer
 * @log_size:  If nonzero, map this size. If zero, figure out the log size
 *             from the superblock and map the correct size. (if no valid
 *             superblock, don't map the log)
 * @read_only: map sb and log read-only if nonzero
 */
static int
famfs_mmap_superblock_and_log_raw(
	const char *devname,
	struct famfs_superblock **sbp,
	struct famfs_log **logp,
	u64 log_size,
	int read_only)
{
	struct famfs_superblock *sb;
	int fd = 0;
	void *sb_buf = NULL;
	int rc = 0;
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	int mapmode  = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;

	assert(sbp); /* superblock pointer mandatory; logp optional */

	if (log_size &&
	    ((log_size < FAMFS_LOG_LEN) || (log_size & (log_size - 1)) )) {
		fprintf(stderr, "%s: invalid log_size %lld\n",
			__func__, log_size);
		return -EINVAL;
	}
	fd = open(devname, openmode, 0);
	if (fd < 0) {
		if (errno == ENOENT)
			fprintf(stderr, "%s: device %s not found\n",
				__func__, devname);
		else
			fprintf(stderr, "%s: open %s failed; rc %d errno %d\n",
				__func__, devname, rc, errno);
		rc = -errno;
		goto err_out;
	}

	/* Map superblock */
	sb_buf = mmap(0, FAMFS_SUPERBLOCK_SIZE, mapmode, MAP_SHARED, fd, 0);
	if (sb_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap superblock from %s\n", devname);
		rc = -1;
		goto err_out;
	}
	sb = (struct famfs_superblock *)sb_buf;
	if (sbp)
		*sbp = sb;

	/* Map the log if requested.
	 * * If log_size is nonzero, we mmap that size.
	 * * If log_size == 0, we get the log size from the superblock, and
	 *   we only map the log if there is a valid superblock
	 */
	if (logp) {
		void *addr;
		u64 lsize = log_size;

		/* Special case: if the log_size arg==0, we figure out the
		 * log size from the superblock, which first requires validating
		 * the superblock */
		if (lsize == 0) {
			invalidate_processor_cache(sb, FAMFS_SUPERBLOCK_SIZE);
			if (famfs_check_super(sb)) {
				/* No valid superblock, and no log_size -
				 * don't map log */
				goto out;
			}

			/* Superblock is valid; get the log size from it */
			lsize = sb->ts_log_len;
		}

		/* Map log */
		addr = mmap(0, lsize, mapmode, MAP_SHARED, fd, FAMFS_LOG_OFFSET);
		if (addr == MAP_FAILED) {
			fprintf(stderr, "Failed to mmap log from %s\n", devname);
			rc = -1;
			goto err_out;
		}
		*logp = (struct famfs_log *)addr;
		invalidate_processor_cache(*logp, lsize);
	}

out:
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
		return 1; /* rc=1: SB may be valid, but is the wrong version */
	}

	sbcrc = famfs_gen_superblock_crc(sb);
	if (sb->ts_crc != sbcrc) {
		fprintf(stderr, "%s ERROR: crc mismatch in superblock!\n",
			__func__);
		return -1;
	}

	if (sb->ts_alloc_unit != 4096 && sb->ts_alloc_unit != 0x200000) {
		fprintf(stderr, "%s: invalid alloc unit in superblock: %lld\n",
			__func__, sb->ts_alloc_unit);
		return -1;
	}
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
		se[i].se_offset = te_list[i].offset;
		se[i].se_len    = te_list[i].len;
	}
	return se;
}

/**
 * famfs_v1_set_file_map()
 *
 * This function attaches an allocated simple extent list to a file
 *
 * @path:
 * @fd: file descriptor for the file whose map will be created (already open)
 * @size:
 * @nextents:
 * @extent_list:
 */
static int
famfs_v1_set_file_map(
	int                         fd,
	size_t                      size,
	int                         nextents,
	struct famfs_simple_extent *ext_list,/* TODO: convert to famfs_log_fmap */
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
		/* The v1 extent list doesn't have an extent_type */
		filemap.ext_list[i].offset = ext_list[i].se_offset;
		filemap.ext_list[i].len    = ext_list[i].se_len;
	}

	rc = ioctl(fd, FAMFSIOC_MAP_CREATE, &filemap);
	if (rc)
		fprintf(stderr, "%s: failed MAP_CREATE for file (errno %d)\n",
			__func__, errno);

	return rc;
}

#if (FAMFS_KABI_VERSION > 42)
/**
 * famfs_v2_set_file_map()
 *
 * This function attaches an allocated simple extent list to a file
 *
 * @path:
 * @fd:  file descriptor for the file whose map will be created (already open)
 * @size:
 * @nextents:
 * @extent_list:
 */
static int
famfs_v2_set_file_map(
	int                          fd,
	size_t                       size,
	const struct famfs_log_fmap *fm,
	enum famfs_file_type         type,
	int                          verbose)
{
	struct famfs_ioc_simple_extent kse[FAMFS_MAX_SIMPLE_EXTENTS] = { 0 };
	struct famfs_ioc_fmap ioc_fmap = { 0 };
	struct famfs_ioc_interleaved_ext kie[FAMFS_MAX_INTERLEAVED_EXTENTS] = { 0 };
	int rc;
	int i;

	assert(fd > 0);

	ioc_fmap.fioc_file_type  = type;
	ioc_fmap.fioc_file_size  = size;
	ioc_fmap.fioc_ext_type   = fm->fmap_ext_type;

	switch (fm->fmap_ext_type) {
	case FAMFS_EXT_SIMPLE: {
		if (fm->fmap_nextents > FAMFS_MAX_SIMPLE_EXTENTS) {
			fprintf(stderr, "%s: extent list overflow (%d)\n",
				__func__, fm->fmap_nextents);
			return -EINVAL;
		}

		ioc_fmap.fioc_nextents   = fm->fmap_nextents;
		for (i = 0; i < fm->fmap_nextents; i++) {
			kse[i].devindex = fm->se[i].se_devindex;
			kse[i].offset   = fm->se[i].se_offset;
			kse[i].len      = fm->se[i].se_len;

			if (verbose > 1)
				printf("%s: devindex=%lld offset=0x%llx "
				       "len=0x%llx\n",
				       __func__, kse[i].devindex,
				       kse[i].offset, kse[i].len);
		}
		ioc_fmap.kse = kse;
		break;
	}
	case FAMFS_EXT_INTERLEAVE: {
		int j;

		ioc_fmap.fioc_niext = fm->fmap_nextents;
		for (i = 0; i < fm->fmap_nextents; i++) {
			const struct famfs_interleaved_ext *ie = &fm->ie[i];

			kie[i].ie_nstrips = ie->ie_nstrips;
			kie[i].ie_chunk_size = ie->ie_chunk_size;
			/* XXX if more than one interleaved extent becomes possible,
			 * we won't just be able to use the file size here
			 */
			kie[i].ie_nbytes = size;

			if (ie->ie_nstrips > FAMFS_MAX_SIMPLE_EXTENTS) {
				fprintf(stderr,
					"%s: strip overflow (%lld) at ie %d\n",
					__func__, ie->ie_nstrips, i);
				return -EINVAL;
			}

			/* Strip extents use the simple extent structures */
			for (j = 0; j < ie->ie_nstrips; j++) {
				kse[j].devindex = ie->ie_strips[j].se_devindex;
				kse[j].offset   = ie->ie_strips[j].se_offset;
				kse[j].len      = ie->ie_strips[j].se_len;
			}
			kie[i].ie_strips = kse; /* strips are simple extents */
		}
		ioc_fmap.kie = kie;
		break;
	}
	default:
		fprintf(stderr, "%s: unrecognized extent list type (%d)\n",
			__func__, fm->fmap_ext_type);
		return -EINVAL;
		break;
	}

	rc = ioctl(fd, FAMFSIOC_MAP_CREATE_V2, &ioc_fmap);
	if (rc)
		fprintf(stderr, "%s: failed MAP_CREATE for file (errno %d)\n",
			__func__, errno);

	return rc;
}
#endif

/**
 * __famfs_mkmeta()
 */
static int
__famfs_mkmeta(
	const char *mpt,
	const struct famfs_superblock *sb,
	const struct famfs_log *logp, /* hmm, not needed */
	enum famfs_system_role role,
	int shadow,
	int verbose)
{
	struct famfs_log_stats ls = {0};
	struct famfs_simple_extent ext = {0};
	char dirpath[PATH_MAX]  = {0};
	char sb_file[PATH_MAX]  = {0};
	char log_file[PATH_MAX] = {0};
	struct stat st = {0};
	int sbfd, logfd;
	int rc;

	assert(sb);
	assert(logp);

	strncat(dirpath, mpt,     PATH_MAX - 1);
	strncat(dirpath, "/",     PATH_MAX - 1);
	strncat(dirpath, ".meta", PATH_MAX - 1);

	/* Create the meta directory */
	if (stat(dirpath, &st) == -1) {
		rc = mkdir(dirpath, 0755);
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
				if (!shadow) /* shadow files aren't "actual size" */
					fprintf(stderr,
						"%s: bad superblock file - "
						"remount likely required\n",
						__func__);
			}
		} else {
			fprintf(stderr,
				"%s: non-regular file found "
				"where superblock expected\n", __func__);
			return -EINVAL;
		}
	}

	if (shadow) {
		/* Create shadow superblock file */
		struct famfs_log_file_meta fm = {0};

		fm.fm_size = FAMFS_SUPERBLOCK_SIZE;
		fm.fm_flags = 0;
		fm.fm_uid = 0;
		fm.fm_gid = 0;
		fm.fm_mode = 0444;
		strncpy((char *)fm.fm_relpath,
			famfs_relpath_from_fullpath(mpt, sb_file),
			FAMFS_MAX_PATHLEN - 1);

		fm.fm_fmap.fmap_ext_type = FAMFS_EXT_SIMPLE;
		fm.fm_fmap.fmap_nextents = 1;

		fm.fm_fmap.se[0].se_offset = 0;
		fm.fm_fmap.se[0].se_len = FAMFS_SUPERBLOCK_SIZE;

		famfs_shadow_file_create(sb_file, &fm, &ls, 0, 0, 0, verbose);
	} else {
		/* Create and provide mapping for Superblock file */
		sbfd = open(sb_file, O_RDWR|O_CREAT,
			    0444 /* sb file is read-only everywhere */);
		if (sbfd < 0) {
			fprintf(stderr, "%s: failed to create file %s\n",
				__func__, sb_file);
			return -1;
		}

		if (file_has_v1_map(sbfd)) {
			fprintf(stderr,
				"%s: found valid superblock file; NOP\n",
				__func__);
		} else {
			ext.se_offset = 0;
			ext.se_len    = FAMFS_SUPERBLOCK_SIZE;
			rc = famfs_v1_set_file_map(sbfd, FAMFS_SUPERBLOCK_SIZE,
						   1, &ext, FAMFS_SUPERBLOCK);
			if (rc) {
				fprintf(stderr,
				    "%s: failed to create superblock file %s\n",
					__func__, sb_file);
				close(sbfd);
				unlink(sb_file);
				return -rc;
			}
		}
		close(sbfd);
	}

	/* Check if log file already exists, and cleanup if bad */
	rc = stat(log_file, &st);
	if (rc == 0) {
		if ((st.st_mode & S_IFMT) == S_IFREG) {
			/* Log file exists; is it the right size? */
			if (st.st_size != sb->ts_log_len) {
				if (!shadow) {
					/* Shadow files are not "actual size" */
					fprintf(stderr,
						"%s: bad log file - "
						"remount likely required\n",
						__func__);
				}
			}
		} else {
			fprintf(stderr,
				"%s: non-regular file found where log expected\n",
				__func__);
			return -EINVAL;
		}
	}

	if (shadow) {
		/* Create shadow log file */
		struct famfs_log_file_meta fm = {0};

		fm.fm_size = sb->ts_log_len;
		fm.fm_flags = 0;
		fm.fm_uid = 0;
		fm.fm_gid = 0;
		fm.fm_mode = (role == FAMFS_MASTER) ? 0644 : 0444;
		strncpy((char *)fm.fm_relpath,
			famfs_relpath_from_fullpath(mpt, log_file),
			FAMFS_MAX_PATHLEN - 1);

		fm.fm_fmap.fmap_ext_type = FAMFS_EXT_SIMPLE;
		fm.fm_fmap.fmap_nextents = 1;
		fm.fm_fmap.se[0].se_offset = sb->ts_log_offset;;
		fm.fm_fmap.se[0].se_len = sb->ts_log_len;

		famfs_shadow_file_create(log_file, &fm, &ls, 0, 0, 0, verbose);
	} else {
		/* Create and provide mapping for log file
		 * Log is only writable on the master node
		 */
		logfd = open(log_file, O_RDWR|O_CREAT,
			     (role == FAMFS_MASTER) ? 0644 : 0444);
		if (logfd < 0) {
			fprintf(stderr, "%s: failed to create file %s\n",
				__func__, log_file);
			return -1;
		}

		if (file_has_v1_map(logfd)) {
			fprintf(stderr,
				"%s: found valid log file; doing nothing\n",
				__func__);
		} else {
			ext.se_offset = sb->ts_log_offset;
			ext.se_len    = sb->ts_log_len;
			rc = famfs_v1_set_file_map(logfd, sb->ts_log_len, 1,
						   &ext, FAMFS_LOG);
			if (rc) {
				fprintf(stderr,
					"%s: failed to create log file %s\n",
					__func__, log_file);
				return -1;
			}
		}
		close(logfd);
	}
	printf("%s: Meta files successfully created\n", __func__);
	return 0;
}
	
/**
 * famfs_mkmeta()
 *
 * Create the meta files (.meta/.superblock and .meta/.log)) in a mounted famfs
 * file system
 *
 * @devname: primary device for a famfs file system
 */
int
famfs_mkmeta(
	const char *devname,
	const char *shadowpath,
	int verbose)
{
	struct famfs_superblock *sb;
	enum famfs_system_role role;
	struct famfs_log *logp;
	char *mpt = NULL;
	int rc;

	rc = famfs_mmap_superblock_and_log_raw(devname, &sb, &logp,
					       0 /* figure out log size */,
					       1 /* Read only */);
	if (rc) {
		fprintf(stderr, "%s: superblock/log access failed\n", __func__);
		return -1;
	}

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock on device %s\n",
			__func__, devname);
		return -1;
	}

	role = famfs_get_role(sb);

	if (shadowpath) {
		rc = __famfs_mkmeta(shadowpath, sb, logp, role,
				    1 /* shadow */, verbose);
	} else {
		/* Get mount point path */
		mpt = famfs_get_mpt_by_dev(devname);
		if (!mpt) {
			fprintf(stderr,
				"%s: unable to resolve mount pt from dev %s\n",
				__func__, devname);
			return -1;
		}

		rc = __famfs_mkmeta(mpt, sb, logp, role,
				    0 /* not shadow */, verbose);
	}

	free(mpt);
	return rc;
}

/**
 * mmap_whole_file()
 *
 * @fname:
 * @read_only: mmap will be read-only if true
 * @size:      size will be stored if this pointer is non-NULL
 *
 * TODO: this is only used by the cli for file verification. Move to CLI?
 * Returns:
 * NULL - failure
 * otherwise - success
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
		fprintf(stderr, "%s: error %s is not a regular file\n",
			__func__, fname);
		return NULL;
	}
	if (sizep)
		*sizep = st.st_size;

	fd = open(fname, openmode, 0);
	if (fd < 0) {
		fprintf(stderr, "open %s failed; rc %d errno %d\n",
			fname, rc, errno);
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
famfs_print_log_stats(
	const char *msg,
	const struct famfs_log_stats *ls,
	int verbose)
{
	printf("%s: %llu log entries; %llu new files; %llu new directories\n",
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
	if (ls->yaml_checked) {
		printf("\tshadow yaml checked for %lld files\n",
		       ls->yaml_checked);

		printf("\tthere were %lld yaml errors detected\n",
			ls->yaml_errs);
	}
}

static inline int
famfs_log_full(const struct famfs_log *logp)
{
	return (logp->famfs_log_next_index > logp->famfs_log_last_index);
}

static inline int
famfs_log_entry_fc_path_is_relative(const struct famfs_log_file_meta *fc)
{
	return ((strlen((char *)fc->fm_relpath) >= 1)
		&& (fc->fm_relpath[0] != '/'));
}

static inline int
famfs_log_entry_md_path_is_relative(const struct famfs_log_mkdir *md)
{
	return ((strlen((char *)md->md_relpath) >= 1)
		&& (md->md_relpath[0] != '/'));
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

int
famfs_validate_log_entry(const struct famfs_log_entry *le, u64 index)
{
	unsigned long crc;
	int errors = 0;
	int retries = 1;

	/* XXX this mock_failure returns 0 which is success */
	if (mock_failure == MOCK_FAIL_GENERIC)
		return 0;

retry_once:
	if (le->famfs_log_entry_seqnum != index) {
		fprintf(stderr, "%s: bad seqnum; expect %lld found %lld\n",
			__func__, index, le->famfs_log_entry_seqnum);
		errors++;
	}
	crc = famfs_gen_log_entry_crc(le);
	if (le->famfs_log_entry_crc != crc) {
		fprintf(stderr, "%s: bad crc at log index %lld\n",
			__func__, index);
		errors++;
	}

	/* If a client node recently played the log, and the processor cache
	 * read ahead, and the master node recently appended the log,
	 * we could be looking at a loaded cache line with stale data.
	 * Invalidate the cache once and try again.
	 */
	if (errors && retries--) {
		invalidate_processor_cache(le, sizeof(*le));
		errors = 0;
		goto retry_once;
	}
	return errors;
}

/**
 * __famfs_logplay()
 *
 * Inner function to play the log for a famfs file system
 * Caller has already validated the superblock and log
 *
 * @mpt:         mount point path (or shadow fs path if shadow==true)
 * @sb:          superblock (may be NULL unless it's a shadow logplay)
 * @logp:        pointer to a read-only copy or mmap of the log
 * @dry_run:     process the log but don't create the files & directories
 * @client_mode: for testing; play the log as if this is a client node,
 *               even on master
 * @shadow:      Play into shadow file system instead (for famfs-fuse)
 * @shadowtest:  When playing to shadow, whether or not the shadow file
 *               already exists, re-ingest the shadow file and verify that
 *               results in an identical 'struct famfs_log_file_meta'
 * @verbose:     verbose flag
 *
 * Returns value: Number of errors detected (0=complete success)
 */
int
__famfs_logplay(
	const char		*mpt,
	const struct famfs_superblock *sb,
	const struct famfs_log	*logp,
	int                     dry_run,
	int                     client_mode,
	int                     shadow,
	int                     shadowtest,
	enum famfs_system_role  role,
	int			verbose)
{

	struct famfs_log_stats ls = { 0 };
	char *shadow_root = NULL;
	int bad_entries = 0;
	u64 i, j;
	int rc;

	if (role == FAMFS_NOSUPER) {
		fprintf(stderr, "%s: no valid superblock on device\n", __func__);
		return -1;
	}
	if (famfs_validate_log_header(logp)) {
		fprintf(stderr, "%s: invalid log header\n", __func__);
		return -1;
	}

	/*
	 * Non-shadow logplay has already verified that mpt exists and is a dir,
	 * because it had to open the meta files. But for shadow logplay,
	 * we need to make sure mpt exists, and create it if not.
	 */
	if (shadow) {
		/* Remember: mpt is supposted to be the shadow path here,
		 * not the mount point
		 */
		shadow_root = famfs_get_shadow_root(mpt, verbose);
		if (!shadow_root) {
			fprintf(stderr,
				"%s: failed to get shadow root from mpt=%s\n",
				__func__, mpt);
			return -1;
		}

		assert(sb);
		rc = __famfs_mkmeta(shadow_root, sb, logp, role, 1, verbose);
		if (rc) {
			free(shadow_root);
			fprintf(stderr, "%s: shadow mkmeta failed\n", __func__);
			return -1;
		}

	}

	if (verbose)
		printf("%s: log contains %lld entries\n",
		       __func__, logp->famfs_log_next_index);

	for (i = 0; i < logp->famfs_log_next_index; i++) {
		struct famfs_log_entry le = logp->entries[i];

		if (famfs_validate_log_entry(&le, i)) {
			fprintf(stderr,
				"%s: Error: invalid log entry at index "
				"%lld of %lld\n",
				__func__, i, logp->famfs_log_next_index);
			bad_entries = 1;
			return -1;
		}
		ls.n_entries++;

		famfs_dump_logentry(&le, i, __func__, verbose);

		switch (le.famfs_log_entry_type) {
		case FAMFS_LOG_FILE: {
			const struct famfs_log_file_meta *fm = &le.famfs_fm;
			char fullpath[PATH_MAX];
			char rpath[PATH_MAX];
			struct stat st;
			int skip_file = 0;
			int fd;

			ls.f_logged++;

			if (!famfs_log_entry_fc_path_is_relative(fm) ||
			    mock_path) {
				fprintf(stderr,
					"%s: ignoring log entry; "
					"path is not relative\n",
					__func__);
				ls.f_errs++;
				skip_file++;
			}

			/* The only file that should have an extent with offset 0
			 * is the superblock, which is not in the log.
			 * Check for files with null offset...
			 */
			for (j = 0; j < fm->fm_fmap.fmap_nextents; j++) {
				const struct famfs_simple_extent *se = &fm->fm_fmap.se[j];

				if (se->se_offset == 0 || mock_path) {
					fprintf(stderr,
						"%s: ERROR file %s "
						"has extent with 0 offset\n",
						__func__, fm->fm_relpath);
					ls.f_errs++;
					skip_file++;
				}
			}

			if (skip_file)
				continue;

			if (shadow) {
				/* For shadow logplay, file path is based on
				 * shadow_root, which may not match mpt
				 */
				snprintf(fullpath, PATH_MAX - 1, "%s/%s",
					 shadow_root,
					 fm->fm_relpath);
				realpath(fullpath, rpath);

				famfs_shadow_file_create(rpath, fm, &ls, 0,
							 dry_run,
							 shadowtest, verbose);
				continue;
			}

			/* Get the rationalized full path */
			snprintf(fullpath, PATH_MAX - 1, "%s/%s", mpt,
				 fm->fm_relpath);
			realpath(fullpath, rpath);


			if (dry_run)
				continue;

			rc = stat(rpath, &st);
			if (!rc) {
				if (verbose > 1)
					fprintf(stderr,
						"%s: File %s exists\n",
						__func__, rpath);
				ls.f_existed++;
				continue;
			}
			if (verbose) {
				printf("%s: creating file %s",
				       __func__, fm->fm_relpath);
				if (verbose > 1)
					printf(" mode %o", fm->fm_mode);

				printf("\n");
			}

			fd = famfs_file_create_stub(rpath, fm->fm_mode,
						    fm->fm_uid, fm->fm_gid,
					       (role == FAMFS_CLIENT) ? 1 : 0);
			if (fd < 0) {
				fprintf(stderr,
					"%s: unable to create destfile (%s)\n",
					__func__, fm->fm_relpath);

				unlink(rpath);
				ls.f_errs++;
				continue;
			}

			/* Build extent list of famfs_simple_extent; the
			 * log entry has a different kind of extent list...
			 */
			if (FAMFS_KABI_VERSION > 42) {
#if (FAMFS_KABI_VERSION > 42)
				rc =  famfs_v2_set_file_map(fd, fm->fm_size,
							    &fm->fm_fmap,
							    FAMFS_REG,
							    verbose);
				if (rc) {
					fprintf(stderr,
						"%s: v2 setmap "
						"failed to create file %s\n",
						__func__, rpath);
				}
#endif
			}
			else {
				struct famfs_simple_extent *el;

				if (fm->fm_fmap.fmap_ext_type != FAMFS_EXT_SIMPLE) {
					fprintf(stderr,
						"%s: error: "
						"non-simple extents in abi 42\n",
						__func__);
					rc = -1;
					goto bad_log_fmap;
				}

				el = calloc(fm->fm_fmap.fmap_nextents,
					    sizeof(*el));
				assert(el);

				for (j = 0; j < fm->fm_fmap.fmap_nextents; j++) {
					const struct famfs_log_fmap *tle = &fm->fm_fmap;

					el[j].se_offset = tle->se[j].se_offset;
					el[j].se_len    = tle->se[j].se_len;
				}
				rc = famfs_v1_set_file_map(fd, fm->fm_size,
						fm->fm_fmap.fmap_nextents,
						el, FAMFS_REG);
bad_log_fmap:
				if (rc)
					fprintf(stderr, "%s: "
						"v1 setmap failed for file %s\n",
						__func__, rpath);
				free(el);
			}


			close(fd);
			ls.f_created++;
			break;
		}
		case FAMFS_LOG_MKDIR: {
			const struct famfs_log_mkdir *md = &le.famfs_md;
			char fullpath[PATH_MAX];
			char rpath[PATH_MAX];
			int skip_dir = 0;
			struct stat st;

			ls.d_logged++;

			if (!famfs_log_entry_md_path_is_relative(md) ||
			    mock_path) {
				fprintf(stderr,
					"%s: ignoring log mkdir entry; "
					"path is not relative\n",
					__func__);
				ls.d_errs++;
				skip_dir++;
			}

			if (skip_dir)
				continue;

			if (dry_run)
				continue;

			snprintf(fullpath, PATH_MAX - 1, "%s/%s",
				 shadow ? shadow_root : mpt,
				 md->md_relpath);
			realpath(fullpath, rpath);

			rc = stat(rpath, &st);
			if (!rc) {
				switch (st.st_mode & S_IFMT) {
				case S_IFDIR:
					/* This is normal for log replay */
					if (verbose > 1) {
						fprintf(stderr,
							"%s: dir %s exists\n",
							__func__, rpath);
					}
					ls.d_existed++;
					break;

				case S_IFREG:
					fprintf(stderr,
						"%s: file (%s) exists "
						"where dir should be\n",
						__func__, rpath);
					ls.d_errs++;
					break;

				default:
					fprintf(stderr, "%s: something (%s) "
						"exists where dir should be\n",
						__func__, rpath);
					ls.d_errs++;
					break;
				}
				continue;
			}

			if (verbose)
				printf("%s: creating directory %s\n",
				       __func__, md->md_relpath);

			rc = famfs_dir_create(shadow ? shadow_root : mpt,
					      (char *)md->md_relpath,
					      md->md_mode,
					      md->md_uid, md->md_gid);
			if (rc) {
				fprintf(stderr, "%s: error: "
					"unable to create directory (%s)\n",
					__func__, md->md_relpath);
				ls.d_errs++;
				continue;
			}

			ls.d_created++;
			break;
		}
		default:
			if (verbose)
				printf("%s: invalid log entry\n", __func__);
			break;
		}
	}
	if (shadow_root)
		free(shadow_root);

	famfs_print_log_stats(shadow ?
			      "famfs_logplay(shadow)" : "famfs_logplay(v1)",
			      &ls, verbose);
	return (bad_entries + ls.f_errs + ls.d_errs + ls.yaml_errs);
}

/**
 * famfs_dax_shadow_logplay()
 *
 * Play the log into a shadow famfs file system directly from a daxdev
 * (Note this is NOT how the log gets played for famfs/fuse file systems -
 * in those, the log is played via the meta files into the shadow directory)
 *
 * @shadowpath:  Root path of shadow file system
 * @dry_run:     Parse and print but don't create shadow files / directories
 * @client_mode: Logplay as client, not master
 * @daxdev:      Dax device to map the superblock and log from
 * @verbose:
 */
int
famfs_dax_shadow_logplay(
	const char   *shadowpath,
	int           dry_run,
	int           client_mode,
	const char   *daxdev,
	int           testmode,
	int           verbose)
{
	enum famfs_system_role role;
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	int rc;

	assert(testmode == 0 || testmode == 1);

	if (!daxdev) {
		fprintf(stderr, "%s: daxdev required\n", __func__);
		return -EINVAL;
	}

	rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, &logp, 0,
					       1 /* read-only */);
	if (rc) {
		fprintf(stderr, "%s: failed to map superblock and log from %s\n",
			__func__, daxdev);
		return rc;
	}
	role = (client_mode) ? FAMFS_CLIENT : famfs_get_role(sb);

	rc = __famfs_logplay(shadowpath, sb, logp, dry_run, client_mode,
			     1 /* shadow mode */,
			     1 + testmode /* shadow */,
			     role, verbose);
	return rc;
}

/**
 * famfs_logplay()
 *
 * Outer function to play the log for a famfs file system
 *
 * This function uses the meta files for the superblock and log, unless
 * "shadow" is set, in which case it reads the sb and log from the daxdev.
 *
 * This function gets and verifies the superblock and log, and then calls the
 * inner logplay function to do the work (in the shadow case,
 * via famfs_shadow_logplay()
 *
 * @fspath:      mount point, or any path within the famfs file system
 * @use_mmap:    Use mmap rather than reading the log into a buffer
 * @dry_run:     process the log but don't create the files & directories
 * @client_mode: For testing; play the log as if this is a client node,
 *               even on master
 * @shadowpath:  Play yaml files into a shadow file system at this path
 * @shadowtest:  Enable shadow test mode
 * @daxdev:      If it's a shadow logplay, get SB and log from this daxdev
 * @verbose:     verbose flag
 */
int
famfs_logplay(
	const char             *fspath,
	int                     use_mmap,
	int                     dry_run,
	int                     client_mode,
	const char             *shadowpath,
	int                     shadowtest,
	const char             *daxdev,
	int                     verbose)
{
	struct famfs_superblock *sb;
	char mpt_out[PATH_MAX];
	char shadow[PATH_MAX];
	struct famfs_log *logp;
	size_t log_size;
	size_t sb_size;
	int role;
	int lfd, sfd;
	int rc;

	if (shadowpath && daxdev)
		return famfs_dax_shadow_logplay(shadowpath, dry_run,
						client_mode, daxdev,
						shadowtest, verbose);

	/* Open log from meta file */
	lfd = open_log_file_read_only(fspath, &log_size, -1, mpt_out, NO_LOCK);
	if (lfd < 0) {
		fprintf(stderr, "%s: failed to open log file for filesystem %s\n",
			__func__, fspath);
		return -1;
	}

	/* Open superblock from meta file */
	sfd = open_superblock_file_read_only(fspath, &sb_size, mpt_out);
	if (sfd < 0) {
		fprintf(stderr,
			"%s: failed to open superblock file for filesystem %s\n",
			__func__, fspath);
		close(lfd);
		return -1;
	}

	if (!shadowpath) {
		if (famfs_path_is_mount_pt(mpt_out, NULL, shadow) && verbose)
			printf("%s: this is logplay for mounted FAMFS_FUSE "
			       "(shadow=%s)\n",
			       __func__, shadow);
	}
	else
		strncpy(shadow, shadowpath, PATH_MAX - 1);

	if (use_mmap) {
		logp = mmap(0, log_size, PROT_READ, MAP_PRIVATE, lfd, 0);
		if (logp == MAP_FAILED) {
			fprintf(stderr,
				"%s: failed to mmap log file for %s\n",
				__func__, mpt_out);
			close(lfd);
			return -1;
		}
		
		sb = mmap(0, FAMFS_SUPERBLOCK_SIZE, PROT_READ,
			  MAP_PRIVATE, sfd, 0);
		if (logp == MAP_FAILED) {
			fprintf(stderr, "%s: "
				"failed to mmap superblock file for %s\n",
				__func__, mpt_out);
			close(lfd);
			close(sfd);
			return -1;
		}
		
		/* Note that this dereferences logp to get the length, and then
		 * invalidates the cache. I think this is ok...
		 */
		invalidate_processor_cache(logp, logp->famfs_log_len);
	} else {
		/* XXX: Hmm, not sure how to invalidate the processor cache
		 * before a posix read. Default is mmap; posix read may not work
		 * correctly for non-cache-coherent configs
		 */
		/* Get log via posix read */
		logp = malloc(log_size);
		if (!logp) {
			close(lfd);
			fprintf(stderr, "%s: malloc %ld failed for log\n",
				__func__, log_size);
			return -ENOMEM;
		}

		rc = famfs_file_read(lfd, (char *)logp, log_size, __func__,
				     "log file", verbose);
		if (rc)
			goto err_out;

		/* Get superblock via posix read */
		sb = calloc(1, sb_size);
		if (!sb) {
			close(sfd);
			close(lfd);
			fprintf(stderr, "%s: malloc %ld failed for superblock\n",
				__func__, log_size);
			return -ENOMEM;
		}

		rc = famfs_file_read(sfd, (char *)sb, sb_size, __func__,
				     "superblock file", verbose);
		if (rc)
			goto err_out;
	}

	role = (client_mode) ? FAMFS_CLIENT : famfs_get_role(sb);

	if (strlen(shadow) > 0)
		rc = __famfs_logplay(shadow, sb, logp, dry_run,
				     client_mode, 1 /* Shadow mode */,
				     shadowtest,
				     role, verbose);
	else
		rc = __famfs_logplay(mpt_out, sb, logp, dry_run,
				     client_mode, 0 /* not shadow mode */,
				     0 /* not shadowtest mode */,
				     role, verbose);
err_out:
	if (use_mmap) {
		munmap(logp, log_size);
		munmap(sb, FAMFS_SUPERBLOCK_SIZE);
	} else {
		free(logp);
		free(sb);
	}
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
 * @logp: pointer to struct famfs_log in memory media
 * @e:    pointer to log entry in memory
 *
 * NOTE: this function is not re-entrant. Must hold a lock or mutexj
 * when calling this function if there is any chance of re-entrancy.
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

	/* Could flush: 1) the log entry, 2) the log header
	 * That would be less flushing, but would not guarantee that the entry
	 * is visible before the log header. If the log header becomes visible
	 * first (leading to reading a cache-incoherent log entry), the checksum
	 * on the log entry will save us - and the logplay can be retried.
	 *
	 * But now we're just flushing the whole log every time...
	 */
	flush_processor_cache(logp, logp->famfs_log_len);

	return 0;
}


/**
 * famfs_relpath_from_fullpath()
 *
 * Returns a pointer to the relpath. This pointer points
 * within the fullpath string
 *
 * @mpt:      mount point string (rationalized by realpath())
 * @fullpath:
 */
static char *
famfs_relpath_from_fullpath(
	const char *mpt,
	char       *fullpath)
{
	char *relpath;

	assert(mpt);
	assert(fullpath);

	if (strstr(fullpath, mpt) != fullpath) {
		/* mpt path should be a substring
		 * starting at the beginning of fullpath*/
		fprintf(stderr,
			"%s: failed to get relpath from mpt=%s fullpath=%s\n",
			__func__, mpt, fullpath);
		return NULL;
	}

	/* Now that we've established that fullpath descends from mpt... */
	assert(strlen(fullpath) >= strlen(mpt));

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
	struct famfs_log            *logp,
	const struct famfs_log_fmap *fmap,
	const char                  *relpath,
	mode_t                       mode,
	uid_t                        uid,
	gid_t                        gid,
	size_t                       size,
	int                          dump_meta)
{
	struct famfs_log_entry le = {0};
	struct famfs_log_file_meta *fm = &le.famfs_fm;

	assert(logp);
	assert(fmap);
	assert(fmap->fmap_nextents >= 1);
	assert(relpath[0] != '/');

	if (famfs_log_full(logp)) {
		fprintf(stderr, "%s: log full\n", __func__);
		return -ENOMEM;
	}

	le.famfs_log_entry_type = FAMFS_LOG_FILE;

	fm->fm_size = size;
	fm->fm_flags = FAMFS_FM_ALL_HOSTS_RW; /* XXX hard coded access for now */

	strncpy((char *)fm->fm_relpath, relpath, FAMFS_MAX_PATHLEN - 1);

	fm->fm_mode = mode;
	fm->fm_uid  = uid;
	fm->fm_gid  = gid;

	fm->fm_fmap.fmap_nextents = fmap->fmap_nextents;
	fm->fm_fmap.fmap_ext_type = fmap->fmap_ext_type;

	memcpy(&fm->fm_fmap, fmap, sizeof(*fmap));

	if (dump_meta)
		famfs_emit_file_yaml(fm, stdout);

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
	struct famfs_log_mkdir *md = &le.famfs_md;

	assert(logp);
	assert(relpath[0] != '/');

	if (famfs_log_full(logp)) {
		fprintf(stderr, "%s: log full\n", __func__);
		return -ENOMEM;
	}

	le.famfs_log_entry_type = FAMFS_LOG_MKDIR;

	strncpy((char *)md->md_relpath, relpath, FAMFS_MAX_PATHLEN - 1);

	md->md_mode = mode;
	md->md_uid  = uid;
	md->md_gid  = gid;

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
			fprintf(stderr, "%s: "
				"path %s appears not to be in a famfs mount\n",
				__func__, path);
			return NULL;
		}

		rpath = realpath(pc, NULL);
		if (rpath)
			return rpath;  /* found a valid path */

		pc = dirname(pc);
		if (--loop_ct == 0) {
			fprintf(stderr, "%s: bailed from possible infinite loop; "
				"path=%s path_copy=%s\n",
				__func__, path, pc);
			return NULL;
		}
	}
	return NULL;
}

/**
 * __open_relpath()
 *
 * This function starts with @path and ascends until @relpath is a valid
 * sub-path from the ascended subset of @path.
 *
 * This is intended for ascending from @path until (e.g.)
 * @relpath=".meta/.superblock" is valid - and opening that.
 *
 * It is also important to verify that the @relpath file is in a famfs
 * file system, but there are also (unit test) cases where it is useful to
 * exercise this logic even if the ascended @path is not in a famfs file system.
 *
 * @path:       any path within a famfs file system (from mount pt on down)
 * @relpath:    the relative path to open (relative to the mount point)
 * @read_only:
 * @size_out:   File size will be returned if this pointer is non-NULL
 * @size_in:    Expected size; -1 is no expected size.
 * @mpt_out:    Mount point will be returned if this pointer is non-NULL
 *              (the string space is assumed to be of size PATH_MAX)
 * @no_fscheck: For unit tests only - don't check whether the file with @relpath
 *              is actually in a famfs file system.
 */
/* XXX: JG: this needs further consideration. It will ascend until relpath
 * is valid (AND rpath is in famfs). But it doesn't guarantee that rpath is
 * the mount point path - and I'm pretty sure we are only interested in opening
 * rpath relative to the mount point path.
 *
 * Possible fix: require that the mount point be passed in in place of "path"...
 */
int
__open_relpath(
	const char    *path,
	const char    *relpath,
	int            read_only,
	size_t        *size_out,
	ssize_t        size_in,
	char          *mpt_out,
	enum lock_opt  lockopt,
	int            no_fscheck)
{
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	char *rpath;
	struct stat st;
	int rc, fd;

	/*
	 * If path does not exist, ascend canonically until we find something
	 * that does exist, or until that remaining path string is too short,
	 * or until it looks like we might be in an infinite loop
	 */
	rpath = find_real_parent_path(path);
	if (!rpath)
		return -1;

	/*
	 * At this point rpath does exist, and is a root-based path. Continue
	 * to ascend as necessary to find the mount point which contains the
	 * meta files
	 */
	while (1) {
		char fullpath[PATH_MAX] = {0};
		int famfs_type;

		rc = stat(rpath, &st);
		if (rc < 0)
			goto next;
		if ((st.st_mode & S_IFMT) == S_IFDIR) {
			/* It's a dir; does it have <relpath> under it? */
			snprintf(fullpath, PATH_MAX - 1, "%s/%s",
				 rpath, relpath);
			rc = stat(fullpath, &st);
			if ((rc == 0) && ((st.st_mode & S_IFMT) == S_IFREG)) {
				/* We found it. */
				famfs_type = file_is_famfs(fullpath);
				if (size_out)
					*size_out = st.st_size;
				if (mpt_out)
					strncpy(mpt_out, rpath, PATH_MAX - 1);
				fd = open(fullpath, openmode, 0);
				free(rpath);

				/* Check whether the file we found is actually
				 * in famfs; Unit tests can disable this check
				 * but production code should not.
				 */
				if (!no_fscheck && famfs_type == NOT_FAMFS) {
					fprintf(stderr,
						"%s: found file %s but it is "
						"not in famfs\n",
						__func__, fullpath);
					close(fd);
					return -1;
				}

				if (lockopt) {
					int operation = LOCK_EX;

					if (lockopt == NON_BLOCKING_LOCK)
						operation |= LOCK_NB;
					rc = flock(fd, operation);
					if (rc) {
						fprintf(stderr,
							"%s: failed to get lock "
							"on %s\n",
							__func__, fullpath);
						close(fd);
						return -1;
					}
				}
				if (size_in != -1 && size_in != st.st_size) {
					fprintf(stderr, "%s: size_in=%ld "
						"size_out=%ld\n", __func__,
						size_in, st.st_size);
					dump_stack();
					assert(1);
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
 * This will traverse upward from path, looking for a directory
 * containing a .meta/.log. If found, it opens the log.
 */
static int
__open_log_file(
	const char *path,
	int         read_only,
	size_t     *sizep,
	ssize_t     size_in,
	char       *mpt_out,
	enum lock_opt lockopt)
{
	return __open_relpath(path, LOG_FILE_RELPATH, read_only, sizep, size_in,
			      mpt_out, lockopt, 0);
}

int
static open_log_file_read_only(
	const char *path,
	size_t     *sizep,
	ssize_t     size_in,
	char       *mpt_out,
	enum lock_opt lockopt)
{
	return __open_log_file(path, 1, sizep, size_in, mpt_out, lockopt);
}

static int
open_log_file_writable(
	const char *path,
	size_t     *sizep,
	ssize_t     size_in,
	char       *mpt_out,
	enum lock_opt lockopt)
{
	return __open_log_file(path, 0, sizep, size_in, mpt_out, lockopt);
}

/*
 * Handlers for opening the meta config file
 */
#if (FAMFS_KABI_VERSION > 42)
static int
__open_cfg_file(
	const char *path,
	int         read_only,
	size_t     *sizep)
{
	return __open_relpath(path, CFG_FILE_RELPATH, read_only,
			      sizep, -1, NULL, NO_LOCK, 0);
}

int
static open_cfg_file_read_only(
	const char *path,
	size_t     *sizep)
{
	return __open_cfg_file(path, 1, sizep);
}

#if 0
static int
open_cfg_file_writable(
	const char *path,
	size_t     *sizep)
{
	return __open_cfg_file(path, 0, sizep);
}
#endif
#endif

/*
 * Handlers for opening the superblock file
 */
static int
__open_superblock_file(
	const char *path,
	int         read_only,
	size_t     *sizep,
	char       *mpt_out)
{
	/* No need to plumb locking for the superblock; use the log for locking */
	return __open_relpath(path, SB_FILE_RELPATH, read_only, sizep,
			      FAMFS_SUPERBLOCK_SIZE,
			      mpt_out, NO_LOCK, 0);
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

	assert(read_only); /* check whether we ever open it writable */

	fd = __open_superblock_file(path, read_only,
				    &sb_size, NULL);
	if (fd < 0) {
		fprintf(stderr, "%s: "
			"failed to open superblock file %s for filesystem %s\n",
			__func__, read_only ? "read-only" : "writable",	path);
		return NULL;
	}
	addr = mmap(0, sb_size, prot, MAP_SHARED, fd, 0);
	close(fd);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap superblock file %s\n",
			__func__, path);
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

	/* XXX: the open is always read-only, but the mmap is sometimes writable;
	 * Why does this work ?!
	 */
	fd = open_log_file_read_only(path, &log_size, -1, NULL, lockopt);
	if (fd < 0) {
		fprintf(stderr,
			"%s: failed to open log file for filesystem %s\n",
			__func__, path);
		return NULL;
	}
	addr = mmap(0, log_size, prot, MAP_SHARED, fd, 0);
	close(fd);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file %s\n",
			__func__, path);
		return NULL;
	}
	/* Should not need to invalidate the cache for the log because we have
	 * verified that we are running on the master, which is the only node
	 * that is allowed to write the log */
	logp = (struct famfs_log *)addr;
	if (log_size != logp->famfs_log_len) {
		fprintf(stderr, "%s: log file length is invalid (%lld / %lld)\n",
			__func__, (s64)log_size, logp->famfs_log_len);
		munmap(addr, log_size);
		return NULL;
	}
	flush_processor_cache(logp, log_size);  /* invalidate processor cache */
	return logp;
}

int
famfs_fsck(
	const char *path,
	int use_mmap,
	int human,
	int force,
	int verbose)
{
	struct famfs_superblock *sb = NULL;
	struct famfs_log *logp = NULL;
	struct stat st;
	int famfs_type;
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
	 * * If a dax device we'll fsck that - but only if the fs is
	 *   not currently mounted.
	 * * If any file path from the mount point on down in a mounted famfs
	 *   file system is specified, we will find the superblock and log files
	 *   and fsck the mounted file system.
	 */
	switch (st.st_mode & S_IFMT) {
	case S_IFCHR: {
		char *mpt;
		/* Check if there is a mounted famfs file system on this device;
		 * fail if so - if mounted, have to fsck by mount pt
		 * rather than by device
		 */
		mpt = famfs_get_mpt_by_dev(path);
		if (mpt) {
			free(mpt);
			if (!force) {
				fprintf(stderr, "%s: error: "
				    "cannot fsck by device (%s) when mounted\n",
					__func__, path);
				return -EBUSY;
			}
			fprintf(stderr,
				"%s: Attempting %s mmap when fs is mounted\n",
				__func__, path);
		}

		/* If it's a device, we'll try to mmap superblock and log
		 * from the device */
		rc = famfs_get_device_size(path, &size, NULL);
		if (rc < 0)
			return -1;

		rc = famfs_mmap_superblock_and_log_raw(path, &sb, &logp,
						       0 /* figure out log size */,
						       1 /* read-only */);

		break;
	}
	case S_IFREG:
	case S_IFDIR: {
		char *mpt = find_mount_point(path);
		char backing_dev[PATH_MAX];
		char shadow_path[PATH_MAX];
		famfs_type = file_is_famfs(path);

		/*
		 * More options: default is to read the superblock and log into
		 * local buffers (which is useful to spot check that posix read
		 * is not broken). But if the use_mmap open is provided, we will
		 * mmap the superblock and logs files rather than reading them
		 * into a local buffer.
		 */

		/* Print the "mounted file system" header */
		printf("famfs fsck:\n");
		printf("  mount point: %s\n", mpt);
		printf("  mount type:  %s\n", famfs_mount_type(famfs_type));
		if (famfs_type == FAMFS_FUSE) {
			if (famfs_path_is_mount_pt(mpt, backing_dev, shadow_path)) {
				printf("  backing dev: %s\n", backing_dev);
				printf("  shadow path: %s\n", shadow_path);
			}
		}
		printf("\n");
		free(mpt);

		if (use_mmap) {
			/* If it's a file or directory, we'll try to mmap the sb
			 * and log from their files
			 *
			 * Note that this tends to fail
			 */
			sb =   famfs_map_superblock_by_path(path,
							    1 /* read only */);
			if (!sb) {
				fprintf(stderr, "%s: "
					"failed to map superblock from file %s\n",
					__func__, path);
				return -1;
			}

			logp = famfs_map_log_by_path(path, 1 /* read only */,
						     NO_LOCK);
			if (!logp) {
				fprintf(stderr,
					"%s: failed to map log from file %s\n",
					__func__, path);
				return -1;
			}
			break;
		} else {
			int sfd;
			int lfd;

			sfd = open_superblock_file_read_only(path, NULL, NULL);
			if (sfd < 0 || mock_failure == MOCK_FAIL_OPEN_SB) {
				fprintf(stderr,
					"%s: failed to open superblock file\n",
					__func__);
				return -1;
			}

			sb = calloc(1, FAMFS_SUPERBLOCK_SIZE);
			assert(sb);

			rc = famfs_file_read(sfd, (char *)sb,
					     FAMFS_SUPERBLOCK_SIZE, __func__,
					     "superblock file", verbose);
			if (rc != 0 || mock_failure == MOCK_FAIL_READ_SB) {
				free(sb);
				close(sfd);
				fprintf(stderr,
					"%s: error %d reading superblock file\n",
					__func__, errno);
				return -errno;
			}
			close(sfd);

			lfd = open_log_file_read_only(path, NULL, -1,
						      NULL, NO_LOCK);
			if (lfd < 0 || mock_failure == MOCK_FAIL_OPEN_LOG) {
				free(sb);
				close(sfd);
				fprintf(stderr,
					"%s: failed to open log file\n",
					__func__);
				return -1;
			}

			logp = calloc(1, sb->ts_log_len);
			assert(logp);

			/* Read a copy of the log */
			rc = famfs_file_read(lfd, (char *)logp, sb->ts_log_len,
					     __func__,
					     "log file", verbose);
			if (rc != 0
			    || mock_failure == MOCK_FAIL_READ_FULL_LOG
			    || mock_failure == MOCK_FAIL_READ_LOG) {
				close(lfd);
				rc = -1;
				fprintf(stderr, "%s: error %d reading log file\n",
					__func__, errno);
				goto err_out;
			}
			close(lfd);
		}
	}
		break;

	case S_IFBLK: /* fallthrough */
	default:
		fprintf(stderr, "invalid path or dax device: %s\n", path);
		return -EINVAL;
	}

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid famfs superblock on device %s\n",
			__func__, path);
		return -1;
	}
	rc = famfs_fsck_scan(sb, logp, human, verbose);
err_out:
	if (!use_mmap && sb)
		free(sb);
	if (!use_mmap && logp)
		free(logp);

	return rc;
}

/**
 * famfs_validate_superblock_by_path()
 *
 * @path
 *
 * Validate the superblock and return the dax device size, or -1 if sb or size
 * invalid
 */
static ssize_t
famfs_validate_superblock_by_path(const char *path, u64 *alloc_unit)
{
	int sfd;
	void *addr;
	size_t sb_size;
	ssize_t daxdevsize;
	struct famfs_superblock *sb;
	int retries = 1;

retry:
	sfd = open_superblock_file_read_only(path, &sb_size, NULL);
	if (sfd < 0)
		return sfd;
#if 1
	/* debug intermittent failures in long tests */
	if (sb_size != FAMFS_SUPERBLOCK_SIZE) {
		fprintf(stderr, ":== %s: bad superblock size for path %s:"
			"fd=%d sb_size=%ld expected=%d\n",
			__func__, path, sfd, sb_size, FAMFS_SUPERBLOCK_SIZE);
		/* See if a retry also gets it wrong: */
		if (retries) {
			retries = 0;
			close(sfd);
			goto retry;
		}
	}
	if (retries == 0)
		fprintf(stderr,
			":== %s: recovered from a bad superblock size\n",
			__func__);
#endif
	assert(sb_size == FAMFS_SUPERBLOCK_SIZE);

	addr = mmap(0, sb_size, PROT_READ, MAP_SHARED, sfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap superblock file\n",
			__func__);
		close(sfd);
		return -1;
	}
	sb = (struct famfs_superblock *)addr;
	 /* Invalidate the processor cache for the superblock */
	invalidate_processor_cache(sb, sb_size);

	if (famfs_check_super(sb)) {
		fprintf(stderr, "%s: invalid superblock\n", __func__);
		return -1;
	}

	if (alloc_unit)
		*alloc_unit = sb->ts_alloc_unit;

	daxdevsize = sb->ts_daxdev.dd_size;
	munmap(sb, FAMFS_SUPERBLOCK_SIZE);
	close(sfd);
	return daxdevsize;
}

/******************************************************************************
 * Locked Log Abstraction
 */

/**
 * famfs_init_locked_log()
 *
 * @lp:     locked_log structure (mandatory)
 * @fspath: The mount point full path, or any full path within a mounted
 *          famfs FS
 * @thread_ct: Threadpool count; 0=none
 * @verbose:
 */
int
famfs_init_locked_log(
	struct famfs_locked_log *lp,
	const char *fspath,
	int thread_ct,
	int verbose)
{
	char shadow[PATH_MAX];
	char mpt[PATH_MAX];
	size_t log_size;
	void *addr;
	int role;
	int rc;

	memset(lp, 0, sizeof(*lp));

	lp->devsize = famfs_validate_superblock_by_path(fspath,
							&(lp->alloc_unit));
	if (lp->devsize < 0)
		return -1;

	/* famfs_get_role also validates the superblock */
	role = famfs_get_role_by_path(fspath, NULL);
	if (role != FAMFS_MASTER) {
		fprintf(stderr,
			"%s: Error not running on FAMFS_MASTER node\n",
			__func__);
		rc = -1;
		goto err_out;
	}

	/* Log file */
	lp->lfd = open_log_file_writable(fspath, &log_size, -1,
					 mpt, BLOCKING_LOCK);
	if (lp->lfd < 0) {
		fprintf(stderr, "%s: Unable to open famfs log for writing\n",
			__func__);
		/* If we can't open the log file for writing, don't allocate */
		rc = lp->lfd;
		goto err_out;
	}
	lp->mpt = strdup(mpt);

	lp->famfs_type = file_is_famfs(fspath);
	if (lp->famfs_type < 0) {
		rc = -1;
		goto err_out;
	}
	if (lp->famfs_type == FAMFS_FUSE) {
		/* Get the shadow path */
		if (!famfs_path_is_mount_pt(lp->mpt, NULL, shadow)) {
			rc = -1;
			goto err_out;
		}
		if (strlen(shadow) == 0) {
			fprintf(stderr, "%s: failed to get shadow path\n",
				__func__);
			rc = -1;
			goto err_out;
		} else {
			lp->shadow_root = famfs_get_shadow_root(shadow,
								verbose);
		}

		if (!lp->shadow_root) {
			fprintf(stderr, "%s: failed to get shadow path\n",
				__func__);
			rc = -1;
			goto err_out;
		}
	}

	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		    lp->lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		rc = -1;
		goto err_out;
	}
	lp->logp = (struct famfs_log *)addr;

	if (thread_ct > 0)
		lp->thp = thpool_init(thread_ct);

#if 1
	/* Been occasionally hitting this assert; get more info */
	if (lp->logp->famfs_log_len != log_size) {
		fprintf(stderr, "%s: *****************************************\n",
			__func__);
		fprintf(stderr, "%s: log size mismatch log hdr %lld != %ld\n",
			__func__, lp->logp->famfs_log_len, log_size);
		//famfs_fsck_scan(sb, lp->logp, 0, 1);
		rc = -1;
		goto err_out;
	}
#endif
	assert(lp->logp->famfs_log_len == log_size);
	 /* Invalidate the processor cache for the log */
	invalidate_processor_cache(lp->logp, log_size);


#if (FAMFS_KABI_VERSION > 42)
	if (FAMFS_KABI_VERSION > 42) {
		struct famfs_interleave_param interleave_param;
		size_t cfg_size;
		FILE *fp;
		int cfd;

		cfd = open_cfg_file_read_only(fspath, &cfg_size);
		if (cfd < 0) /* Missing cfg file is not an error */
			goto out;

		fp =  fdopen(cfd, "r");
		if (!fp) {
			close(cfd);
			goto out;
		}
		rc = famfs_parse_alloc_yaml(fp, &interleave_param, 1);
		fclose(fp);
		close(cfd);
		if (rc) {
			fprintf(stderr, "%s: failed to parse alloc yaml\n",
				__func__);
			goto out;
		}
		rc = famfs_validate_interleave_param(&interleave_param,
						     lp->alloc_unit,
						     lp->devsize, verbose);
		if (rc == 0) {
			if (verbose)
				printf("%s: good interleave_param metadata!\n",
				       __func__);
			memcpy(&lp->interleave_param, &interleave_param,
			       sizeof(interleave_param));
		}
	}
out:
#endif
	return 0;

err_out:
	if (lp->lfd)
		close(lp->lfd);
	if (lp->thp)
		thpool_destroy(lp->thp);
	return rc;
}

/**
 * famfs_release_locked_log()
 *
 * Unlock the famfs metadata, free locked_log resources, 
 * @lp:
 * @abort: Abort thread pool operations if true
 * @verbose:
 */
int
famfs_release_locked_log(struct famfs_locked_log *lp, int abort, int verbose)
{
	int rc;

	if (lp->bitmap)
		free(lp->bitmap);

	assert(lp->lfd > 0);
	rc = flock(lp->lfd, LOCK_UN);
	if (rc)
		fprintf(stderr, "%s: unlock returned an error\n", __func__);

	close(lp->lfd);
	if (lp->mpt)
		free(lp->mpt);
	if (lp->shadow_root)
		free(lp->shadow_root);
	if (lp->thp) {
		if (abort)
			fprintf(stderr,
				"%s: aborting thread pool due to error(s)\n",
			       __func__);
		else if (verbose)
			printf("%s: waiting for threadpool to complete\n",
			       __func__);

		if (!abort)
			thpool_wait(lp->thp);
		thpool_destroy(lp->thp);
		if (verbose)
			printf("%s: threadpool work complete\n",
			       __func__);	
	}
	return rc;
}

/**
 * famfs_file_create_stub()
 *
 * Create a famfs v1 stub file but don't allocate dax space yet. File map
 * will be added later.
 *
 * @path:
 * @mode:
 * @uid:           used if both uid and gid are non-null
 * @gid:           used if both uid and gid are non-null
 * @disable_write: if this flag is non-zero, write permissions will be removed
 *                 from the mode
 *                 (we default files to read-only on client systems)
 *
 * Returns a file descriptior or -EBADF if the path is not in a famfs file system
 *
 * TODO: append "_empty" to function name
 */
static int
famfs_file_create_stub(
	const char *path,
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
	if (!__file_is_famfs_v1(fd)) {
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
 * test_shadow_yaml()
 *
 * This function parses-back yaml file yaml, into a temporary struct
 * famfs_log_file_meta, and verifies that it exactly matches the original.
 * This is only intended for testing yaml generation and parsing.
 */
static int
famfs_test_shadow_yaml(FILE *fp, const struct famfs_log_file_meta *fc, int verbose)
{
	struct famfs_log_file_meta readback = { 0 };
	int rc;

	rewind(fp);
	rc = famfs_parse_shadow_yaml(fp, &readback, FAMFS_MAX_SIMPLE_EXTENTS,
				     FAMFS_MAX_SIMPLE_EXTENTS, verbose);
	if (rc) {
		struct famfs_log_file_meta readback2 = { 0 };

		fprintf(stderr, "-----------------------------------------------------\n");
		rewind(fp);
		rc = famfs_parse_shadow_yaml(fp, &readback2,
					     FAMFS_MAX_SIMPLE_EXTENTS,
					     FAMFS_MAX_INTERLEAVED_EXTENTS,
					     verbose + 4);
		fprintf(stderr, "%s: failed to parse shadow file yaml\n",
			__func__);
		assert(0);
		return -1;
	}

	/* Make sure the read-back of the yaml results in an identical
	 * struct famfs_log_file_meta */
	if (memcmp(fc, &readback, sizeof(readback))) {
		if (verbose)
			fprintf(stderr,
				"%s: famfs_log_file_meta miscompare "
				"(verbose=2 for more info)\n",
				__func__);
		if (verbose > 2) {
			famfs_emit_file_yaml(&readback, stderr);
			fprintf(stderr, "============\n");
			famfs_emit_file_yaml(fc, stderr);
			//diff_text_buffers(); //XXX
		}
		if (verbose > 1)
			famfs_compare_log_file_meta(fc, &readback, 1);
		return -1;
	}
	if (verbose)
		printf("%s: shadow yaml good!\n", fc->fm_relpath);
	return 0;
}

static int
famfs_shadow_file_create(
	const char                       *shadow_fullpath,
	const struct famfs_log_file_meta *fc,
	struct famfs_log_stats           *ls,
	int 	                          disable_write,
	int                               dry_run,
	int                               testmode,
	int                               verbose)
{
	FILE *fp;
	struct stat st;
	int rc = 0;
	int fd;

	assert(fc);
	//assert(ls);
	if (verbose)
		famfs_emit_file_yaml(fc, stdout);

	if (dry_run)
		return 0;

	rc = stat(shadow_fullpath, &st);
	if (!rc) {
		switch (st.st_mode & S_IFMT) {
		case S_IFDIR:
			fprintf(stderr,
				"%s: directory where file %s expected\n",
				__func__, shadow_fullpath);
			if (ls) ls->f_errs++; /* Should be a file */
			return -1;

		case S_IFREG:
			/* This is normal for log replay */
			/* TODO: options to verify and fix contents? */
			if (verbose > 1)
				fprintf(stderr,
					"%s: file (%s) exists where dir should be\n",
					__func__, shadow_fullpath);
			if (ls) ls->f_existed++;

			/* TODO: detect a yaml mismatch in the existing file */
			if (!testmode)
				return 0;

			/* We're in testmode: open the existing yaml so we
			 * can test it */
			fd = open(shadow_fullpath, O_RDONLY);
			if (fd < 0) {
				fprintf(stderr,
					"%s: open of %s failed, fd %d with %s\n",
					__func__, shadow_fullpath, fd,
					strerror(errno));
				if (ls) ls->f_errs++;
				return -1;
			}

			/* Open shadow file as a stream */
			fp = fdopen(fd, "r");
			if (!fp) {
				fprintf(stderr, "%s: fdopen failed\n", __func__);
				close(fd);
				return -1;
			}
			break;

		default:
			fprintf(stderr,
				"%s: something (%s) exists where shadow file should be\n",
				__func__, shadow_fullpath);
			if (ls) ls->f_errs++;
			return -1;
		}
	} else {
		/* This is a new yaml shadow file */

		fd = open(shadow_fullpath, O_RDWR | O_CREAT, 0644);
		if (fd < 0) {
			fprintf(stderr,
				"%s: open/creat of %s failed, fd %d with %s\n",
				__func__, shadow_fullpath, fd, strerror(errno));
			if (ls) ls->f_errs++;
			return -1;
		}
		/* Open shadow file as a stream */
		fp = fdopen(fd, "w");
		if (!fp) {
			fprintf(stderr, "%s: fdopen failed\n", __func__);
			close(fd);
			return -1;
		}

		/* Write the yaml metadata to the shadow file */
		rc = famfs_emit_file_yaml(fc, fp);
		if (ls) ls->f_created++;
	}

	if (testmode) {
		if (ls) ls->yaml_checked++;
		rc = famfs_test_shadow_yaml(fp, fc, verbose);
		if (rc) {
			/* In yaml testmode, yaml errrs are file errors */
			if (ls) ls->yaml_errs++;
			if (ls) ls->f_errs++;
		}
	}
	fclose(fp);
	close(fd);

	return rc;
}

/**
 * __famfs_mkfile()
 *
 * Inner function to create *and* allocate a file, and logs it.
 *
 * @locked_logp - We have a writable lock, which also means we're running on
 *                the master node
 * @filename    - filename, which may be relative to getcwd(), which is
 *                NOT NECESSARILY relative to the mount point
 * @mode
 * @mode
 * @uid
 * @gid
 * @size
 * @open_existing: If true, and the file already exists and is the right size,
 *                 a file descriptor for the file will be returned. This is
 *                 useful for restarting failed __famfs_cp()
 * @verbose
 *
 * Returns an open file descriptor if successful.
 * On failure, returns:
 *  0 - The operation failed but it's not fatal to a multi-file operationn
 * <0 - The operation failed due to a fatal condition like log full or out
 *      of space, so multi-file operations should abort
 */
int
__famfs_mkfile(
	struct famfs_locked_log *lp,
	const char              *filename,
	mode_t                   mode,
	uid_t                    uid,
	gid_t                    gid,
	size_t                   size,
	int                      open_existing,
	int                      verbose)
{
	struct famfs_log_fmap *fmap = NULL;
	struct famfs_log *logp;
	char *target_fullpath;
	char *relpath = NULL;
	char mpt[PATH_MAX];
	char *cwd = get_current_dir_name();
	struct stat st;
	int fd = -1;
	int rc;

	assert(lp);
	assert(size > 0);
	assert(cwd);

	/* If it's a relative path, append it to "cwd" to make it absolute */
	if (filename[0] != '/') {
		size_t len;

		len = strlen(cwd) + strlen(filename) + 2; /* add slash and null term */

		assert (len < PATH_MAX);
		target_fullpath = malloc(len);
		assert(target_fullpath);

		snprintf(target_fullpath, PATH_MAX - 1, "%s/%s", cwd, filename);
		free(cwd);
		cwd = NULL;
	}
	else
		target_fullpath = strdup(filename);

	/* From here on, use target_fullpath and not filename */

	/* TODO: */
	/* Don't create the file yet, but...
	 * 1. File must not exist
	 * 2. Parent path must exist
	 * 3. Parent path must be in a famfs file system
	 * Otherwise fail
	 */
	if (stat(target_fullpath, &st) == 0) {
		if (open_existing &&
		    S_ISREG(st.st_mode) && st.st_size == size) {
			fd = open(target_fullpath, O_RDWR, mode);
			if (fd < 0) {
				fprintf(stderr,
					"%s: existing open failed %s\nerrno=%d",
					__func__, target_fullpath, errno);
			}
			goto out;
		}
		fprintf(stderr, "%s: Error: file %s already exists\n", __func__,
			target_fullpath);
		fd = -1;
		goto out;
	} else {
		char *tmp_path = strdup(target_fullpath);
		char *parent_path = dirname(tmp_path);

		rc = stat(parent_path, &st);
		free(tmp_path);
		if (rc != 0) {
			fprintf(stderr,
				"%s: Error %s parent dir does not exist\n",
				__func__, target_fullpath);
			fd = -1;
			goto out;
		}
		switch (st.st_mode & S_IFMT) {
		case S_IFDIR:
			break; /* all good - parent is a directory */
		default:
			fprintf(stderr, "%s: Error %s parent exists but is not a directory\n",
				__func__, target_fullpath);
			fd = -1;
			goto out;
		}

		/* TODO: verify parent_path is in a famfs mount */
	}

	logp = lp->logp;
	strncpy(mpt, lp->mpt, PATH_MAX - 1);

	rc = famfs_file_alloc(lp, size, &fmap, verbose);
	if (rc) {
		fprintf(stderr, "%s: famfs_file_alloc(%s, size=%ld) failed\n",
			__func__, target_fullpath, size);
		fd = -1;
		goto out;
	}
	assert(fmap->fmap_nextents == 1);

	relpath = famfs_relpath_from_fullpath(mpt, target_fullpath);
	if (!relpath) {
		fd = -1;
		goto out;
	}

	if (lp->shadow_root) {
		struct famfs_log_file_meta fmeta = { 0 };
		char shadowpath[PATH_MAX];
		char filepath[PATH_MAX];

		fmeta.fm_size = size;
		fmeta.fm_flags = FAMFS_FM_ALL_HOSTS_RW;
		fmeta.fm_uid = uid;
		fmeta.fm_gid = gid;
		fmeta.fm_mode = mode;

		memcpy(&fmeta.fm_fmap, fmap, sizeof(*fmap));

		snprintf(shadowpath, PATH_MAX - 1, "%s/%s", lp->shadow_root,
			 relpath);
		snprintf(filepath, PATH_MAX - 1, "%s/%s", lp->mpt, relpath);
		assert(strlen(relpath) < (sizeof(fmeta.fm_relpath) - 1));
		strncpy(fmeta.fm_relpath, relpath, sizeof(fmeta.fm_relpath) - 1);

		rc = famfs_shadow_file_create(shadowpath, &fmeta, NULL,
					      0, 0, 0, verbose);
		if (rc)
			goto out;

		fd = open(filepath, O_RDWR, mode);
		if (fd < 0)
			fprintf(stderr, "%s: unable to open brand new file %s\n",
				__func__, filepath);
	} else {
		/* Create the stub file in standalone famfs
		 * KABI 42: Only supports simple extent lists
		 * KABI 43: Supports interleaved extents too
		 */
		fd = famfs_file_create_stub(filename, mode, uid, gid, 0);
		if (fd <= 0)
			goto out;

		if (!mock_kmod) {
			if (FAMFS_KABI_VERSION > 42) {
#if (FAMFS_KABI_VERSION > 42)
				rc =  famfs_v2_set_file_map(fd, size, fmap,
							    FAMFS_REG,
							    verbose);
				if (rc) {
					close(fd);
					fd = rc;
					fprintf(stderr,
						"%s: failed to create destination file %s\n",
						__func__, filename);
				}
#endif
			} else {
				struct famfs_simple_extent ext = {0};

				if (fmap->fmap_nextents > 1) {
					fprintf(stderr,
						"%s: nextents %d (are you running a v2 test?)\n",
						__func__, fmap->fmap_nextents);
					close(fd);
					fd = -EINVAL; /* XXX: gotta fix up return codes */
					goto out;
				}
				ext.se_offset = fmap->se[0].se_offset;
				ext.se_len    = fmap->se[0].se_len;

				/* This will do legacy allocation regardless of whether
				 * striping is configured in the ll */
				rc = famfs_v1_set_file_map(fd, size, 1, &ext, FAMFS_REG);
				if (rc) {
					close(fd);
					fd = rc;
					goto out;
				}
			}
		}
	}

	/* TODO: accumulate log entries into a buffer, to be committed upon
	 * release_locked_log() (prior to releasing the lock)
	 */
	/* Log the file creation */
	rc = famfs_log_file_creation(logp, fmap,
				     relpath, mode, uid, gid, size,
				     (verbose > 1) ? 1:0 /* dump metadata */);
	if (rc)
		return rc;


out:
	if (fmap)
		free(fmap);
	if (cwd)
		free(cwd);
	if (target_fullpath)
		free(target_fullpath);

	return fd;
}

/**
 * famfs_mkfile()
 *
 * Do the full job of creating and allocating a famfs file
 */
int
famfs_mkfile(
	const char       *filename,
	mode_t            mode,
	uid_t             uid,
	gid_t             gid,
	size_t            size,
	struct famfs_interleave_param *interleave_param,
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

	rc = famfs_init_locked_log(&ll, filename, 0, verbose);
	if (rc)
		return rc;

	if (interleave_param) {
		if (verbose)
			printf("%s: overriding interleave_param defaults (nbuckets/nstrips/chunk)="
			       "(%lld/%lld/%lld) with (%lld/%lld/%lld)\n", __func__,
			       ll.interleave_param.nbuckets, ll.interleave_param.nstrips,
			       ll.interleave_param.chunk_size,
			       interleave_param->nbuckets, interleave_param->nstrips,
			       interleave_param->chunk_size);

		ll.interleave_param = *interleave_param;
	}
	rc  = __famfs_mkfile(&ll, filename, mode, uid, gid, size, 0, verbose);

	famfs_release_locked_log(&ll, 0, verbose);
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
 * This should become the mid-level mkdir function; verify that target is a
 * directory with a parent that exists and is in a famfs FS. Inner function
 * should rely on these checks, and use the famfs_locked_log.
 *
 * Inner function would also be callled by 'cp -r' (which doesn't exist quite
 * yet)
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
	char mpt_out[PATH_MAX] = { 0 };
	char realdirpath[PATH_MAX];
	char realparent[PATH_MAX];
	char fullpath[PATH_MAX];
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
		fprintf(stderr, "%s: parent path (%s) stat failed\n",
			__func__, parentdir);
	} else if ((st.st_mode & S_IFMT) != S_IFDIR) {
		fprintf(stderr,
			"%s: parent (%s) of path %s is not a directory\n",
			__func__, dirpath, parentdir);
		rc = -1;
		goto err_out;
	}

	/* Parentdir exists and is a directory; rationalize the path with
	 * realpath */
	if (realpath(parentdir, realparent) == 0) {
		fprintf(stderr,
			"%s: failed to rationalize parentdir path (%s)\n",
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
		fprintf(stderr,
			"%s: failed to create mount point dir: EALREADY\n",
			__func__);
		rc = -1;
		goto err_out;
	}

	/* The only difference between a mkdir in FAMFS_V1 and FAMFS_FUSE is
	 * that in the latter case we mkdir in the shadow file system.
	 */
	rc = famfs_dir_create((lp->famfs_type == FAMFS_FUSE) ?
			      lp->shadow_root : mpt_out,
			      relpath, mode, uid, gid);
	if (rc) {
		fprintf(stderr, "%s: failed to mkdir %s\n", __func__, fullpath);
		rc = -1;
		goto err_out;
	}

	/* Should it be logged before it's locally created? */
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

	rc = famfs_init_locked_log(&ll, abspath, 0, verbose);
	if (rc) {
		free(cwd);
		return rc;
	}

	rc = __famfs_mkdir(&ll, dirpath, mode, uid, gid, verbose);

	famfs_release_locked_log(&ll, 0, verbose);
	free(cwd);
	return rc;
}

/**
 * famfs_make_parent_dir()
 *
 * Recurse upwards through the path till we find a directory that exists.
 * On the way back down, create the missing directories for "mkdir -p".
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
			fprintf(stderr, "%s: path %s is not a directory\n",
				__func__, path);
			return -1;
		}
	}

	/* get parent path */
	parentdir = dirname(dirdupe);
	 /* Recurse up :D */
	rc = famfs_make_parent_dir(lp, parentdir, mode, uid, gid,
				   depth + 1, verbose);
	if (rc) {
		fprintf(stderr, "%s: bad path component above (%s)\n",
			__func__, path);
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

	/* dirpath as an indeterminate number of nonexistent dirs, under a path
	 * that must exist. But the immediate parent may not exist. All existing
	 * elements in the path must be dirs. By opening the log, we can get the
	 * mount point path...
	 */

	if (dirpath[0] == '/')
		strncpy(abspath, dirpath, PATH_MAX - 1);
	else
		snprintf(abspath, PATH_MAX - 1, "%s/%s", cwd, dirpath);

	if (verbose)
		printf("%s: cwd %s abspath %s\n", __func__, cwd, abspath);

	rpath = find_real_parent_path(abspath);
	if (!rpath) {
		fprintf(stderr, "%s: failed to find real parent dir\n",
			__func__);
		return -1;
	}

	/* OK, we know were in a FAMFS instance. get a locked log struct */
	rc = famfs_init_locked_log(&ll, rpath, 0, verbose);
	if (rc) {
		free(rpath);
		return rc;
	}

	/* Now recurse up fromm abspath till we find an existing parent,
	 * and mkdir back down */
	rc = famfs_make_parent_dir(&ll, abspath, mode, uid, gid, 0, verbose);

	/* Separate function should release ll and lock */
	famfs_release_locked_log(&ll, 0, verbose);
	free(rpath);
	if (cwd)
		free(cwd);

	return rc;
}


int fd_invalid(int fd) {
	return (!(fcntl(fd, F_GETFD) != -1 || errno != EBADF));
}


struct copy_files {
	char *srcname;
	char *destname;
	int srcfd;
	int destfd;
	char *destp;
	int nchunks;
	int refcount;
	pthread_mutex_t mutex;
};

struct copy_data {
	struct copy_files *cf;
	size_t offset;
	size_t size;
	int verbose;
};

static int
__famfs_copy_file_data(struct copy_data *cp)
{
	size_t chunksize, remainder, offset;
	char *destp;
	pid_t pid = gettid();
	ssize_t bytes;
	int errs = 0;
	int cleanup = 0;
	int rc = 0;
	
	assert(cp);
	assert(cp->cf);

	if (fd_invalid(cp->cf->srcfd)) {
		fprintf(stderr, "%s: bad srcfd\n", __func__);
		errs++;
	}
	if (fd_invalid(cp->cf->destfd)) {
		fprintf(stderr, "%s: bad destfd\n", __func__);
		errs++;
	}
	assert(!errs);

	/* Copy the data */
	chunksize = 0x100000; /* 1 MiB copy chunks */
	offset = cp->offset;
	remainder = cp->size;
	destp = cp->cf->destp;

	for ( ; remainder > 0; ) {
		size_t cur_chunksize = MIN(chunksize, remainder);

		if (cp->verbose > 1)
			printf("%s: %d copy %ld bytes at offset %lx\n",
			       __func__, pid, cur_chunksize, offset);

		/* read into mmapped destination */
		bytes = pread(cp->cf->srcfd, &destp[offset], cur_chunksize,
			      offset);
		if (bytes < 0) {
			fprintf(stderr, "%s: copy fail: "
				"ofs %ld cur_chunksize %ld remainder %ld\n",
				__func__, offset, cur_chunksize, remainder);
			fprintf(stderr, "rc=%ld errno=%d\n", bytes, errno);
			rc = -1;
			goto out;
		}
		if (bytes < cur_chunksize) {
			fprintf(stderr, "%s: short read: "
				"ofs %ld cur_chunksize %ld remainder %ld\n",
				__func__, offset, cur_chunksize, remainder);
			assert(bytes == cur_chunksize);
		}

		/* Update offset and remainder */
		offset += bytes;
		remainder -= bytes;
	}
	/* Flush the processor cache for the dest file */
	flush_processor_cache(destp, cp->size);

	pthread_mutex_lock(&cp->cf->mutex);
	if (--cp->cf->refcount == 0) {
		printf("famfs cp: 100%%: %s\n", cp->cf->destname);
		cleanup++;
	} else if (cp->verbose) {
		int percent = ((cp->cf->nchunks - cp->cf->refcount) * 100) /
			cp->cf->nchunks;

		printf("progress:  %02d%%: %s\n", percent, cp->cf->destname);
	}
	pthread_mutex_unlock(&cp->cf->mutex);

out:
	if (cleanup) {
		/* cf is shared and can't be cleaned up until all threads
		 * have finished with it */
		free(cp->cf->srcname);
		free(cp->cf->destname);
		munmap(destp, cp->size);
		close(cp->cf->srcfd);
		close(cp->cf->destfd);
		pthread_mutex_destroy(&cp->cf->mutex);
		free(cp->cf);
	}
	free(cp); /* cp is not shared */
	return rc;
}

/* This void/void wrapper is needed by the thread pool */
void
__famfs_threaded_copy(void *arg)
{
	__famfs_copy_file_data((struct copy_data *)arg);
}

#define CP_CHUNKSIZE (128 * 0x100000) /* 128 MiB */

static int
famfs_copy_file_data(
	struct famfs_locked_log *lp,
	const char *srcname,
	const char *destname,
	int srcfd,
	int destfd,
	size_t size,
	int verbose)
{
	size_t chunk_size = (CP_CHUNKSIZE) ? CP_CHUNKSIZE : size;
	size_t nchunks = (size + chunk_size - 1) / chunk_size;
	struct copy_files *cf;
	struct copy_data *cp;
	int rc;

	cf = calloc(1, sizeof(*cf));
	assert(cf);

	cf->srcname = strdup(srcname);
	cf->destname = strdup(destname);
	cf->srcfd = srcfd;
	cf->destfd = destfd;
	pthread_mutex_init(&cf->mutex, NULL);

	/* Memory map the range we need */
	cf->destp = mmap(0, size, PROT_READ | PROT_WRITE,
			 MAP_SHARED, destfd, 0);
	assert(cf->destp != MAP_FAILED);

	/* if thpool_add_work returns an error, fall back */
	if (lp->thp) {
		size_t remainder, offset, this_chunk;

		remainder = size;
		offset = 0;
		cf->refcount = nchunks;
		cf->nchunks = nchunks;

		if (verbose && nchunks > 1)
			printf("famfs cp: %s: "
			       "%ld bytes, %ld chunks in threaded copy\n",
			       destname, size, nchunks);

		for (; remainder > 0; ) {
			cp = calloc(1, sizeof(*cp));
			assert(cp);

			this_chunk = MIN(remainder, chunk_size);

			cp->cf = cf;
			cp->verbose = verbose;

			cp->offset = offset;
			cp->size = this_chunk;

			/* cp is freed by __famfs_threaded_copy() */
			if (mock_threadpool)
				rc = __famfs_copy_file_data(cp);
			else
				rc = thpool_add_work(lp->thp,
						     __famfs_threaded_copy,
						     cp);

			assert(rc == 0);

			remainder -= this_chunk;
			offset += this_chunk;
		}
		return 0;
	}

	cp = calloc(1, sizeof(*cp));
	assert(cp);

	cf->refcount = 1;
	cp->cf = cf;
	cp->offset = 0;
	cp->size = size;
	cp->verbose = verbose;

	return __famfs_copy_file_data(cp);
}

/**
 * __famfs_cp()
 *
 * Inner file copy function
 *
 * Copy a file from any file system into famfs. A destination file is created
 * and allocated, and the data is copied info it.
 *
 * @lp       - famfs_locked_log struct
 * @srcfile  - must exist and be a regular file
 * @destfile - must not exist (and will be a regular file).
 *             If @destfile does not fall within a famfs file system, we will
 *             clean up and fail
 * NOTE: paths may be absolute or relative to getcwd()
 * @mode     - If mode is NULL, mode is inherited fro msource file
 * @uid
 * @gid
 * @verbose
 *
 * Return values:
 * 0  - Success
 * >0 - Something failed but if it is a multi-file copy, it should continue
 * <0 - A failure that should cause multi-file operations to bail out (such as
 *      out of space or log full...
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
	int rc, srcfd, destfd;
	struct stat srcstat;

	assert(lp);

	/* Validate source file */
	rc = stat(srcfile, &srcstat);
	if (rc) {
		fprintf(stderr, "%s: unable to stat srcfile (%s)\n",
			__func__, srcfile);
		return 1; /* not an abort condition */
	}
	switch (srcstat.st_mode & S_IFMT) {
	case S_IFREG:
		/* Source is a file - all good */
		if (srcstat.st_size == 0) {
			if (verbose > 1)
				fprintf(stderr, "%s: skipping empty file %s\n",
					__func__, srcfile);

			return 1;
		}
		break;

	case S_IFDIR:
		/* source is a directory; fail for now
		 * (should this be mkdir? Probably...
		 * at least if it's a recursive copy)
		 */
		fprintf(stderr,
			"%s: -r not specified; omitting directory '%s'\n",
			__func__, srcfile);
		return 1;

	default:
		fprintf(stderr,
			"%s: error: src %s is not a regular file\n",
			__func__, srcfile);
		return 1;
	}

	/*
	 * Make sure we can open and read the source file
	 */
	srcfd = open(srcfile, O_RDONLY, 0);
	if (srcfd < 0 || mock_failure == MOCK_FAIL_OPEN) {
		fprintf(stderr, "%s: unable to open srcfile (%s)\n",
			__func__, srcfile);
		return 1;
	}

	/* Create the destination file; if it exists and is the right size,
	 * go ahead and copy into it...
	 */
	destfd = __famfs_mkfile(lp, destfile,
				(mode == 0) ? srcstat.st_mode : mode,
				uid, gid, srcstat.st_size,
				1, /* accept existing file if size is right */
				verbose);
	if (destfd <= 0)
		return destfd;

	/* famfs_copy_file_data will close the file descriptors and unmap
	 * the destination when it finishes */
	return famfs_copy_file_data(lp, srcfile, destfile, srcfd, destfd,
				    srcstat.st_size, verbose);
}

/**
 * famfs_cp()
 *
 * Mid layer file copy function
 *
 * @lp       - Locked Log struct (required)
 * @srcfile  - skipped unless it's a regular file
 * @destfile - Nonexistent file, or existing directory. If destfile is a
 *             directory, this function fill append basename(srcfile) to
 *             destfile to get a nonexistent file path
 * @verbose
 */
int
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
	 * * An existing path to a directory in famfs
	 */
	rc = stat(destfile, &deststat);
	if (!rc) {
		switch (deststat.st_mode & S_IFMT) {
		case S_IFDIR: {
			char destpath[PATH_MAX];
			char src[PATH_MAX];

			if (verbose > 1)
				printf("%s: (%s) -> (%s/)\n", __func__,
				       srcfile, destfile);

			/* Destination is directory;  get the realpath and
			 * append the basename from the source
			 */
			if (realpath(destfile, destpath) == 0 ||
					mock_failure == MOCK_FAIL_GENERIC) {
				fprintf(stderr,
					"%s: failed to rationalize dest path "
					"(%s)\n", __func__, destfile);
				return 1;
			}
			strncpy(src, srcfile, PATH_MAX - 1);
			snprintf(destpath, PATH_MAX - 1, "%s/%s",
				 destfile, basename(src));
			strncpy(actual_destfile, destpath, PATH_MAX - 1);
			break;
		}
		default:
		strncpy(actual_destfile, destfile, PATH_MAX - 1);
#if 0
			fprintf(stderr, "%s: error: dest file (%s) exists "
				"and is not a directory\n",
				__func__, destfile);
			return -EEXIST;
#endif
		}
	} else {
		/* File does not exist;
		 * the check whether it is in famfs will happen after the
		 * file is created
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
 * @dest     - Must be a directory if it exists; if dest does not exist,
 *             its parent dir is required to exist.
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
			/* Recursive copy can't recover from a mkdir failure */
			return rc;
		}
	}

	directory = opendir(src);
	if (directory == NULL) {
		/* XXX is it possible to get here since we
		 * created the dir if it didn't exist? */
		fprintf(stderr, "%s: failed to open src dir (%s)\n",
			__func__, src);
		return 1;
	}

	/* Loop through the directry entries */
	while ((entry = readdir(directory)) != NULL) {
		char srcfullpath[PATH_MAX];
		struct stat src_stat;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
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
			rc = famfs_cp(lp, srcfullpath, dest,
				      mode, uid, gid, verbose);
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

			snprintf(newdirpath, PATH_MAX - 1, "%s/%s",
				 dest, basename(src_copy));
			/* Recurse :D
			 * Parent of newdirpath is guaranteed to exist, because
			 * that's a property of this recursion
			 */
			rc = famfs_cp_dir(lp, srcfullpath, newdirpath,
					  mode, uid, gid, verbose);
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
 *   * In the 2 arg case, last arg can be either a directory or a non-existent
 *     file name
 *   * Files will be copied to their basename in the last-arg directory
 *   * Everything that can be copied according to these rules will be copied
 *     (but the return value will be 1 if anything failed
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
	struct famfs_interleave_param *s,
	int recursive,
	int thread_ct,
	int verbose)
{
	struct famfs_locked_log ll = { 0 };
	char *dest = argv[argc - 1];
	int src_argc = argc - 1;
	char *dirdupe   = NULL;
	char *parentdir = NULL;
	char *dest_parent_path;
	struct stat st;
	int err = 0;
	int rc;
	int i;

	rc = stat(dest, &st);
	if (rc == 0) {
		/* Destination exists */
		switch (st.st_mode & S_IFMT) {
		case S_IFDIR:
			/* It's a directory - all good */
			dest_parent_path = realpath(dest, NULL);
			break;
		default:
			if (argc == 2) {
				/* Special case: destination can be an
				 * existing file if argc == 2
				 */
				dirdupe = strdup(dest);
				parentdir = dirname(dirdupe);
				dest_parent_path = realpath(parentdir, NULL);
				if (!dest_parent_path) {
					free(dirdupe);
					fprintf(stderr,
						"%s: unable to get realpath for (%s)\n",
						__func__, dest);
					return -1;
				}

				break;
			}
			fprintf(stderr,
				"%s: Error: destination (%s) "
				"exists and is not a directory\n",
				__func__, dest);
			return -1;
		}
	}
	else {
		/* dest nonexistent; its parent is the parentdir (must exist) */
		dirdupe = strdup(dest);
		parentdir = dirname(dirdupe);
		dest_parent_path = realpath(parentdir, NULL);
		if (!dest_parent_path) {
			free(dirdupe);
			fprintf(stderr, "%s: unable to get realpath for (%s)\n",
				__func__, dest);
			return -1;
		}

		/* Check to see if the parent of the destination (last arg)
		 * is a directory. if not, error out
		 */
		rc = stat(dest_parent_path, &st);
		if (!rc) {
			switch (st.st_mode & S_IFMT) {
			case S_IFDIR:
				/* It's a directory - all good */
				break;
			default:
				fprintf(stderr,
					"%s: Error: dest parent (%s) "
					"exists and is not a directory\n",
					__func__, dest_parent_path);
				free(dest_parent_path);
				free(dirdupe);
				return -1;
			}
		}
	}

	/* If this is a recursive request, or if argc > 2, the destination must
	 * be a directory, although it need not exist yet. But if the
	 * destination exists and is not a dir, that's an error
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
					"%s: Error: destination (%s) "
					"exists and is not a directory\n",
					__func__, dest_parent_path);
				free(dest_parent_path);
				free(dirdupe);
				return -1;
			}
		}
	}

	rc = famfs_init_locked_log(&ll, dest_parent_path, thread_ct, verbose);
	if (rc) {
		free(dest_parent_path);
		free(dirdupe);
		return rc;
	}

	if (s) {
		if (verbose)
			printf("%s: overriding interleave_param defaults "
			       "(nbuckets/nstrips/chunk)="
			       "(%lld/%lld/%lld) with (%lld/%lld/%lld)\n",
			       __func__,
			       ll.interleave_param.nbuckets,
			       ll.interleave_param.nstrips,
			       ll.interleave_param.chunk_size,
			       s->nbuckets, s->nstrips, s->chunk_size);

		ll.interleave_param = *s;
	}

	for (i = 0; i < src_argc; i++) {
		struct stat src_stat;

		/* Need to handle source files and directries differently */
		rc = stat(argv[i], &src_stat);
		if (rc) {
			fprintf(stderr, "famfs cp: cannot stat '%s': ",
				argv[i]);
			perror("");
			err = 1;
			goto err_out;
		}
		if (verbose)
			printf("%s:  %s\n", __func__, argv[i]);

		switch (src_stat.st_mode & S_IFMT) {
		case S_IFREG:
			/* Dest is a directory and files will be copied into it */
			rc = famfs_cp(&ll, argv[i], dest, mode,
				      uid, gid, verbose);
			if (rc < 0) { /* rc < 0 is errors we abort after */
				fprintf(stderr,
					"%s: aborting copy due to error\n",
					argv[i]);
				err = rc;
				goto err_out;
			}
			if (rc) /* rc > 0 is errors that we continue after */
				err = 1; /* if anything failed, return 1 */
			break;

		case S_IFDIR:
			if (recursive) {
				/* Parent is guaranteed to exist,
				 * we verified it above */
				rc = famfs_cp_dir(&ll, argv[i], dest, mode, uid,
						  gid, verbose);
				if (rc < 0) { /* rc < 0 is abort errors */
					fprintf(stderr,
						"%s/: aborting copy due to error\n",
						argv[i]);
					err = rc;
					goto err_out;
				}
				if (rc)  /* rc > 0 is errors that we continue after */
					err = 1;
			} else {
				fprintf(stderr,
					"%s: -r not specified; omitting directory '%s'\n",
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
	free(dirdupe);
	famfs_release_locked_log(&ll, (err < 0) ? 1 : 0, /* abort on err < 0 */
				 verbose);
	free(dest_parent_path);
	return err;
}

/**
 * famfs_clone()
 *
 *
 * This function is for generating cross-linked file errors, and should be
 * compiled out of the library when not needed for that purpose.
 */
int
famfs_clone(const char *srcfile,
	    const char *destfile,
	    int   verbose)
{
	struct famfs_simple_extent *se = NULL;
	struct famfs_ioc_map filemap = {0};
	struct famfs_extent *ext_list = NULL;
	uuid_le src_fs_uuid, dest_fs_uuid;
	struct famfs_log_fmap fmap = {0};
	char srcfullpath[PATH_MAX];
	char destfullpath[PATH_MAX];
	int src_role, dest_role;
	char mpt_out[PATH_MAX];
	struct famfs_log *logp;
	char *relpath = NULL;
	struct stat src_stat;
	size_t log_size;
	int lfd = 0;
	int sfd = 0;
	int dfd = 0;
	void *addr;
	int rc;
	int i;

	/* srcfile must already exist; Go ahead and check that first */
	if (realpath(srcfile, srcfullpath) == NULL) {
		fprintf(stderr, "%s: bad source path %s\n", __func__, srcfile);
		return -1;
	}
	/* and srcfile must be in famfs... */
	if (!file_is_famfs_v1(srcfullpath)) {
		fprintf(stderr,
			"%s: source path (%s) not in a famfs v1 file system\n",
			__func__, srcfullpath);
		return -1;
	}
	rc = stat(srcfullpath, &src_stat);
	if (rc < 0 || mock_failure == MOCK_FAIL_GENERIC) {
		fprintf(stderr, "%s: unable to stat srcfile %s\n",
			__func__, srcfullpath);
		return -1;
	}

	/*
	 * Need to confirm that both files are in the same file system.
	 * Otherwise, the cloned extents will be double-invalid on the
	 * second file :(
	 */
	src_role = famfs_get_role_by_path(srcfile, &src_fs_uuid);
	dest_role = famfs_get_role_by_path(destfile, &dest_fs_uuid);
	if (src_role < 0 || mock_failure == MOCK_FAIL_SROLE) {
		fprintf(stderr,
			"%s: Error: unable to check role for src file %s\n",
			__func__, srcfullpath);
		return -1;
	}
	if (dest_role < 0 ) {
		fprintf(stderr,
			"%s: Error: unable to check role for dest file %s\n",
			__func__, destfile);
		return -1;
	}
	if ((src_role != dest_role) ||
	    memcmp(&src_fs_uuid, &dest_fs_uuid, sizeof(src_fs_uuid)) != 0 ||
	    mock_failure == MOCK_FAIL_ROLE) {
		fprintf(stderr,
			"%s: Error: source and destination "
			"must be in the same file system\n",
			__func__);
		return -1;
	}
	if (src_role != FAMFS_MASTER) {
		fprintf(stderr,
			"%s: file creation not allowed on client systems\n",
			__func__);
		return -EPERM;
	}
	/* FAMFS_MASTER role now confirmed, and the src and destination
	 * are in the same famfs */

	/* Open source file */
	sfd = open(srcfullpath, O_RDONLY, 0);
	if (sfd < 0 || mock_failure == MOCK_FAIL_OPEN) {
		fprintf(stderr, "%s: failed to open source file %s\n",
			__func__, srcfullpath);

		/* close the sfd if control reached here due to a mock_failure */
		if (sfd > 0 && mock_failure == MOCK_FAIL_OPEN) {
			rc = -1;
			goto err_out;
		}
		return -1;
	}

	/*
	 * Get map for source file
	 */
	/* Get the map, which includes the extent count */
	rc = ioctl(sfd, FAMFSIOC_MAP_GET, &filemap);
	if (rc) {
		fprintf(stderr, "%s: MAP_GET returned %d errno %d\n",
			__func__, rc, errno);
		goto err_out;
	}

	/* Now that we have the extent count, we can get the extent list */
	ext_list = calloc(filemap.ext_list_count, sizeof(struct famfs_extent));
	rc = ioctl(sfd, FAMFSIOC_MAP_GETEXT, ext_list);
	if (rc) {
		fprintf(stderr, "%s: GETEXT returned %d errno %d\n",
			__func__, rc, errno);
		goto err_out;
	}

	/*
	 * For this operation we need to open the log file, which also gets us
	 * the mount point path
	 */
	lfd = open_log_file_writable(srcfullpath, &log_size, -1,
				     mpt_out, BLOCKING_LOCK);
	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		rc = -1;
		goto err_out;
	}
	logp = (struct famfs_log *)addr;

	/* Clone is only allowed on the master, so we don't need to invalidate
	 * the cache */

	/* Create the destination file. This will be unlinked later if we
	 * don't get all the way through the operation.
	 */
	dfd = famfs_file_create_stub(destfile, src_stat.st_mode,
				     src_stat.st_uid, src_stat.st_gid, 0);
	if (dfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n",
			__func__, destfile);
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
	rc = famfs_v1_set_file_map(dfd, filemap.file_size,
				   filemap.ext_list_count,
				   se, FAMFS_REG);
	if (rc) {
		fprintf(stderr, "%s: failed to create destination file %s\n",
			__func__, destfile);
		goto err_out;
	}

	/* Now have created the destination file (and therefore we know it
	 * is in a famfs mount, we need its relative path of
	 */
	assert(realpath(destfile, destfullpath));

	relpath = famfs_relpath_from_fullpath(mpt_out, destfullpath);
	if (!relpath) {
		rc = -1;
		unlink(destfullpath);
		goto err_out;
	}

	assert(filemap.extent_type == SIMPLE_DAX_EXTENT);
	fmap.fmap_ext_type = FAMFS_EXT_SIMPLE;
	fmap.fmap_nextents = filemap.ext_list_count;
	for (i = 0; i< filemap.ext_list_count; i++) {
		fmap.se[i].se_offset = se[i].se_offset;
		fmap.se[i].se_len    = se[i].se_len;
	}

	rc = famfs_log_file_creation(logp, &fmap,
				     relpath, src_stat.st_mode,
				     src_stat.st_uid, src_stat.st_gid,
				     filemap.file_size, 0);
	if (rc) {
		fprintf(stderr,
			"%s: failed to log caller-specified allocation\n",
			__func__);
		rc = -1;
		unlink(destfullpath);
		goto err_out;
	}

	rc = 0;
	close(lfd); /* Closing releases the lock */
	lfd = 0;
	/***************/

err_out:
	free(ext_list);
	free(se);
	if (lfd > 0)
		close(lfd);
	if (sfd > 0)
		close(sfd);
	if (dfd > 0)
		close(dfd);
	return rc;
}

/**
 * __famfs_mkfs()
 *
 * This handller can be called by unit tests; the actual device open/mmap is
 * done by the caller, so an alternate caller can arrange for a superblock
 * and log to be written to alternate files/locations.
 */
int
__famfs_mkfs(const char              *daxdev,
	     struct famfs_superblock *sb,
	     struct famfs_log        *logp,
	     u64                      log_len,
	     u64                      device_size,
	     int                      force,
	     int                      kill)

{
	int rc;

	/* Minimum log length is the FAMFS_LOG_LEN; Also, must be a power of 2 */
	if (log_len & (log_len - 1) || log_len < FAMFS_LOG_LEN) {
		fprintf(stderr, "Error: invalid log length (%lld)\n", log_len);
		return -EINVAL;
	}

	/* This test is redundant with famfs_mfks(), but is kept because that
	 * function can't be called by unit tests (because it opens the
	 * actual device)
	 */
	if (kill && force) {
		printf("Famfs superblock killed\n");
		sb->ts_magic = 0;
		flush_processor_cache(sb, sb->ts_log_offset);
		return 0;
	}

	/* Bail if there is a valid superblock and force is not set;
	 * We already verifed (if there is a superblock) that we are running
	 * on the master
	 */
	invalidate_processor_cache(sb, FAMFS_SUPERBLOCK_SIZE);
	rc = famfs_check_super(sb);
	if ((rc == 0 || rc == 1) && !force) {
		/* rc == 0 is good superblock, rc == 1 a is possibly good SB at
		 * wrong version, which still should not be overwritten without
		 * 'force'
		 */
		fprintf(stderr,
			"Device %s already has a famfs superblock\n", daxdev);
		return -1;
	}

	rc = famfs_get_system_uuid(&sb->ts_system_uuid);
	if (rc) {
		fprintf(stderr, "mkfs.famfs: unable to get system uuid");
		return -1;
	}
	sb->ts_magic      = FAMFS_SUPER_MAGIC;
	sb->ts_version    = FAMFS_CURRENT_VERSION;
	sb->ts_log_offset = FAMFS_LOG_OFFSET;
	sb->ts_log_len    = log_len;
	sb->ts_alloc_unit = FAMFS_ALLOC_UNIT; /* Future: make configurable */
	sb->ts_omf_ver_major = FAMFS_OMF_VER_MAJOR;
	sb->ts_omf_ver_minor = FAMFS_OMF_VER_MINOR;
	famfs_uuidgen(&sb->ts_uuid);

	/* Check for bad / non-writable daxdev */
	if (sb->ts_magic != FAMFS_SUPER_MAGIC) {
		fprintf(stderr,
			"%s: Error: primary daxdev (%s) is not writable\n",
			__func__, daxdev);
		return -1;
	}

	/* Note: generated UUIDs are ok now, but we will need to use
	 * tagged-capacity UUIDs when CXL3 provdies UUIDs as tags */
	famfs_uuidgen(&sb->ts_dev_uuid);

	/* Configure the first daxdev */
	sb->ts_daxdev.dd_size = device_size;
	strncpy(sb->ts_daxdev.dd_daxdev, daxdev, FAMFS_DEVNAME_LEN);

	/* Calculate superblock crc */
	sb->ts_crc = famfs_gen_superblock_crc(sb); /* gotta do this last! */

	/* Zero and setup the log */
	memset(logp, 0, log_len);
	logp->famfs_log_magic      = FAMFS_LOG_MAGIC;
	logp->famfs_log_len        = log_len;
	logp->famfs_log_next_seqnum = 0;
	logp->famfs_log_next_index = 0;
	logp->famfs_log_last_index = (((log_len - offsetof(struct famfs_log, entries))
				      / sizeof(struct famfs_log_entry)) - 1);

	logp->famfs_log_crc = famfs_gen_log_header_crc(logp);
	famfs_fsck_scan(sb, logp, 1, 0);

	/* Force a writeback of the log followed by the superblock */
	flush_processor_cache(logp, logp->famfs_log_len);
	flush_processor_cache(sb, FAMFS_SUPERBLOCK_SIZE);
	return 0;
}

int
famfs_mkfs(const char *daxdev,
	   u64         log_len,
	   int         kill,
	   int         force)
{
	int rc;
	size_t devsize;
	enum famfs_extent_type type = SIMPLE_DAX_EXTENT;
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	u64 min_devsize = 4ll * 1024ll * 1024ll * 1024ll;
	char *mpt = NULL;

	mpt = famfs_get_mpt_by_dev(daxdev);
	if (mpt) {
		fprintf(stderr, "%s: cannot mkfs while %s is mounted on %s\n",
				__func__, daxdev, mpt);
		free(mpt);
		return -1;
	}

	if (kill && force) {
		rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, NULL,
						       0, 0 /*read/write */);
		if (rc) {
			fprintf(stderr, "Failed to mmap superblock\n");
			return -1;
		}
		printf("Famfs superblock killed\n");
		sb->ts_magic = 0;
		flush_processor_cache(&sb->ts_magic, sizeof(sb->ts_magic));
		return 0;
	}

	rc = famfs_get_role_by_dev(daxdev);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to establish role\n", __func__);
		return rc;
	}

	/* If the role is FAMFS_CLIENT, there is a superblock already;
	 * if the role is not FAMFS_CLIENT, its' either FAMFS_MASTER or
	 * FAMFS_NOSUPER; In either of those cases it's ok to mkfs.
	 *
	 * If the role is FAMFS_CLIENT, they'll have to manually blow away
	 * the superblock if they want to do a new mkfs.
	 */
	if (rc == FAMFS_CLIENT) {
		fprintf(stderr, "Error: Device %s has a superblock owned by"
				" another host.\n", daxdev);
		return rc;
	}

	rc = famfs_get_device_size(daxdev, &devsize, &type);
	if (rc)
		return -1;

	printf("devsize: %ld\n", devsize);

	if (devsize < min_devsize) {
		fprintf(stderr, "%s: unsupported memory device size (<4GiB)\n",
			__func__);
		return -EINVAL;
	}

	/* Either there is no valid superblock, or the caller is the master */

	rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, &logp,
					       log_len, 0 /* read/write */);
	if (rc)
		return -1;

	return __famfs_mkfs(daxdev, sb, logp, log_len, devsize, force, kill);
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
		/* XXX is it possible to get here since we created the dir
		 * if it didn't exist? */
		fprintf(stderr, "%s: failed to open src dir (%s)\n",
			__func__, dirpath);
		return -1;
	}

	/* Loop through the directry entries */
	while ((entry = readdir(directory)) != NULL) {
		char fullpath[PATH_MAX];
		struct famfs_ioc_map filemap = {0};
		int fd;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(fullpath, PATH_MAX - 1, "%s/%s",
			 dirpath, entry->d_name);
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
				continue;
			}
			rc = ioctl(fd, FAMFSIOC_MAP_GET, &filemap);
			if (rc) {
				fprintf(stderr,
					"%s: Error file not mapped: %s\n",
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
			rc = famfs_recursive_check(fullpath, &nfiles_out,
						   &ndirs_out,
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
	char shadow_out[PATH_MAX];
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
		fprintf(stderr, "%s: must use absolute path of mount point\n",
			__func__);
		return -1;
	}

	if (!famfs_path_is_mount_pt(path, dev_out, shadow_out)) {
		fprintf(stderr, "%s: path (%s) is not a famfs mount point\n",
			__func__, path);
		return -1;
	}

	snprintf(metadir, PATH_MAX - 1, "%s/.meta", path);
	snprintf(sbpath, PATH_MAX - 1, "%s/.meta/.superblock", path);
	snprintf(logpath, PATH_MAX - 1, "%s/.meta/.log", path);
	rc = stat(metadir, &st);
	if (rc) {
		fprintf(stderr,
			"%s: Need to run mkmeta on device %s for this file system\n",
			__func__, dev_out);
		ndirs++;
		return -1;
	}
	rc = stat(sbpath, &st);
	if (rc) {
		fprintf(stderr,
			"%s: superblock file not found for file system %s\n",
			__func__, path);
		nerrs++;
	}

	rc = stat(logpath, &st);
	if (rc) {
		fprintf(stderr, "%s: log file not found for file system %s\n",
			__func__, path);
		nerrs++;
	}

	rc = famfs_recursive_check(path, &nfiles_out, &ndirs_out,
				   &nerrs_out, verbose);
	nfiles += nfiles_out;
	ndirs += ndirs_out;
	nerrs += nerrs_out;
	printf("%s: %lld files, %lld directories, %lld errors\n",
	       __func__, nfiles, ndirs, nerrs);
	return rc;
}
