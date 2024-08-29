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

extern int mock_uuid;
/**
 * get_multiplier()
 *
 * For parsing numbers on command lines with K/M/G for KiB etc.
 */
s64 get_multiplier(const char *endptr)
{
	size_t multiplier = 1;

	if (!endptr)
		return 1;

	switch (*endptr) {
	case 'k':
	case 'K':
		multiplier = 1024;
		break;
	case 'm':
	case 'M':
		multiplier = 1024 * 1024;
		break;
	case 'g':
	case 'G':
		multiplier = 1024 * 1024 * 1024;
		break;
	case 0:
		return 1;
	}
	++endptr;
	if (*endptr) /* If the unit was not the last char in string, it's an error */
		return -1;
	return multiplier;
}

void
famfs_dump_logentry(
	const struct famfs_log_entry *le,
	const int index,
	const char *prefix,
	int verbose)
{
	int i;

	if (!verbose)
		return;

	switch (le->famfs_log_entry_type) {
	case FAMFS_LOG_FILE: {
		const struct famfs_file_meta *fm = &le->famfs_fm;
		const struct famfs_log_fmap *fmap = &fm->fm_fmap;

		printf("%s: %d file=%s size=%lld\n", prefix, index,
		       fm->fm_relpath, fm->fm_size);

		switch (fmap->fmap_ext_type) {
		case FAMFS_EXT_SIMPLE:
			if (verbose > 1) {
				printf("\tFAMFS_EXT_SIMPLE:\n");
				for (i = 0; i < fmap->fmap_nextents; i++)
					printf("\text: %d tofs=0x%llx len=0x%llx\n",
					       i, fmap->se[i].se_offset, fmap->se[i].se_len);
			}
			break;

		case FAMFS_EXT_INTERLEAVE:
			/* XXX */
			printf("%s: Error: Write some code to dump interleaved fmaps!\n", prefix);
			break;
		default:
			printf("\tError unrecognized extent type\n");
		}
		break;
	}

	case FAMFS_LOG_MKDIR: {
		const struct famfs_mkdir *md = &le->famfs_md;
		printf("%s: mkdir: %o %d:%d: %s \n", prefix,
		       md->md_mode, md->md_uid, md->md_gid, md->md_relpath);
		break;
	}

	case FAMFS_LOG_DELETE:
	default:
		printf("\tError unrecognized log entry type\n");
	}
}

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

	printf("famfs log: (%p)\n", logp);
	printf("\tmagic:      %llx\n", logp->famfs_log_magic);
	printf("\tlen:        %lld\n", logp->famfs_log_len);
	printf("\tlast index: %lld\n", logp->famfs_log_last_index);
	printf("\tnext index: %lld\n", logp->famfs_log_next_index);
}

#define SYS_UUID_DIR "/opt/famfs"
#define SYS_UUID_FILE "system_uuid"

int
famfs_get_system_uuid(uuid_le *uuid_out)
{
	FILE *f;
	char uuid_str[48];  /* UUIDs are 36 characters long, plus null terminator */
	uuid_t uuid;
	char sys_uuid_file_path[PATH_MAX] = {0};

	snprintf(sys_uuid_file_path, PATH_MAX - 1, "%s/%s",
			SYS_UUID_DIR, SYS_UUID_FILE);

	if (famfs_create_sys_uuid_file(sys_uuid_file_path) < 0) {
		fprintf(stderr, "Failed to create system-uuid file\n");
		return -1;
	}
	f = fopen(sys_uuid_file_path, "r");
	if (f == NULL) {
		fprintf(stderr, "%s: unable to open system uuid at %s\n",
				__func__, sys_uuid_file_path);
		return -errno;
	}

	/* gpt */
	if (fscanf(f, "%36s", uuid_str) != 1 || mock_uuid) {
		fprintf(stderr, "%s: unable to read system uuid at %s, errno: %d\n",
				__func__, sys_uuid_file_path, errno);
		fclose(f);
		unlink(sys_uuid_file_path);
		return -1;
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

void
famfs_uuidgen(uuid_le *uuid)
{
	uuid_t local_uuid;

	uuid_generate(local_uuid);
	memcpy(uuid, &local_uuid, sizeof(local_uuid));
}

void
famfs_print_uuid(const uuid_le *uuid)
{
	uuid_t local_uuid;
	char uuid_str[37];

	memcpy(&local_uuid, uuid, sizeof(local_uuid));
	uuid_unparse(local_uuid, uuid_str);

	printf("%s\n", uuid_str);
}

/*
 * Check if uuid file exists, if not, create it
 * and update it with a new uuid.
 *
 */
int famfs_create_sys_uuid_file(char *sys_uuid_file)
{
	int uuid_fd, rc;
	char uuid_str[37];  /* UUIDs are 36 char long, plus null terminator */
	uuid_t local_uuid;
	struct stat st = {0};
	uuid_le sys_uuid;

	/* Do nothing if file is present */
	rc = stat(sys_uuid_file, &st);
	if (rc == 0 && (st.st_mode & S_IFMT) == S_IFREG)
		return 0;

	/* File not found, check for directory */
	rc = stat(SYS_UUID_DIR, &st);
	if (rc < 0 && errno == ENOENT) {
		/* No directory found, create one */
		rc = mkdir(SYS_UUID_DIR, 0755);
		if (rc || mock_uuid) {
			fprintf(stderr, "%s: error creating dir %s errno: %d\n",
				__func__, SYS_UUID_DIR, errno);
			return -1;
		}
	}

	uuid_fd = open(sys_uuid_file, O_RDWR | O_CREAT, 0444);
	if (uuid_fd < 0) {
		fprintf(stderr, "%s: failed to open/create %s errno %d.\n",
			__func__, sys_uuid_file, errno);
		return -1;
	}

	famfs_uuidgen(&sys_uuid);
	memcpy(&local_uuid, &sys_uuid, sizeof(sys_uuid));
	uuid_unparse(local_uuid, uuid_str);
	rc = write(uuid_fd, uuid_str, 37);
	if (rc < 0 || mock_uuid) {
		fprintf(stderr, "%s: failed to write uuid to %s, errno: %d\n",
			__func__, sys_uuid_file, errno);
		unlink(sys_uuid_file);
		return -1;
	}
	return 0;
}

int
famfs_flush_file(const char *filename, int verbose)
{
	struct stat st;
	size_t size;
	void *addr;
	int rc;

	rc = stat(filename, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: file not found (%s)\n", __func__, filename);
		return 3;
	}
	if ((st.st_mode & S_IFMT) != S_IFREG) {
		if (verbose)
			fprintf(stderr, "%s: not a regular file: (%s)\n", __func__, filename);
		return 2;
	}

	/* Only flush regular files */

	addr = famfs_mmap_whole_file(filename, 1, &size);
	if (!addr)
		return 1;

	if (verbose > 1)
		printf("%s: flushing: %s\n", __func__, filename);

	/* We don't know caller needs a flush or an invalidate, so barriers on both sides */
	hard_flush_processor_cache(addr, size);
	return 0;
}
