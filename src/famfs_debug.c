// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */
/*
 * The intent of this file, famfs_debug.c, is to isolate debug functions
 * for which we don't care about code coverage.
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

#include "famfs_meta.h"
#include "famfs_lib.h"
#include "famfs_lib_internal.h"

#define MSG_SIZE 8192

#include <execinfo.h>

void dump_stack()
{
	void *buffer[100];
	int nptrs = backtrace(buffer, 100);
	char **strings = backtrace_symbols(buffer, nptrs);

	if (strings == NULL) {
		perror("backtrace_symbols");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < nptrs; i++)
		printf("%s\n", strings[i]);

	free(strings);
}

static int
famfs_compare_simple_ext_list(
	char *msgbuf,
	int next,
	const struct famfs_simple_extent *se1,
	const struct famfs_simple_extent *se2)
{
	char *tmpbuf = calloc(1, PATH_MAX);
	int errs = 0;
	int i;

	for (i = 0; i < next; i++) {
		if (memcmp(&se1[i], &se2[i], sizeof(se1[i]))) {
			sprintf(tmpbuf, "ext %d mismatch\n", i);
			strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
			errs++;
		}
	}
	return errs;
}

int
famfs_compare_log_file_meta(
	const struct famfs_log_file_meta *m1,
	const struct famfs_log_file_meta *m2,
	int verbose)
{
	char *msgbuf = calloc(1, 8192);
	char *tmpbuf = calloc(1, PATH_MAX);
	int errs = 0;
	u32 j;

	if (m1->fm_size != m2->fm_size) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_size mismatch %lld / %lld",
			 m1->fm_size, m2->fm_size);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_flags != m2->fm_flags) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_flags mismatch %x / %x\n",
			 m1->fm_flags, m2->fm_flags);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_uid != m2->fm_uid) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_uid mismatch %d / %d\n",
			 m1->fm_uid, m2->fm_uid);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_gid != m2->fm_gid) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_gid mismatch %d / %d\n",
			 m1->fm_gid, m2->fm_gid);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_mode != m2->fm_mode) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_mode mismatch %o / %o\n",
			 m1->fm_mode, m2->fm_mode);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (strcmp(m1->fm_relpath, m2->fm_relpath) != 0) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_relpath mismatch %s / %s\n",
			 m1->fm_relpath, m2->fm_relpath);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}

	if (verbose && errs) {
		fprintf(stderr, "%s", msgbuf);
		msgbuf[0] = 0;
	}

	if (m1->fm_fmap.fmap_ext_type != m2->fm_fmap.fmap_ext_type) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_ext_type mismatch %d / %d\n",
			 m1->fm_fmap.fmap_ext_type, m2->fm_fmap.fmap_ext_type);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
		return errs;
	}

	switch (m1->fm_fmap.fmap_ext_type) {
	case FAMFS_EXT_SIMPLE:
		errs += famfs_compare_simple_ext_list(msgbuf,
						      m1->fm_fmap.fmap_nextents,
						      m1->fm_fmap.se,
						      m2->fm_fmap.se);
		break;
	case FAMFS_EXT_INTERLEAVE:
		for (j = 0; j < m1->fm_fmap.fmap_niext; j++) {
			if (m1->fm_fmap.ie[j].ie_nstrips != m2->fm_fmap.ie[j].ie_nstrips) {
				snprintf(tmpbuf, PATH_MAX - 1,
					 "ie[%d].nstrips mismatch %lld / %lld\n", j,
					 m1->fm_fmap.ie[j].ie_nstrips,
					 m2->fm_fmap.ie[j].ie_nstrips);
				strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
				errs++;
			}
			if (m1->fm_fmap.ie[j].ie_chunk_size != m2->fm_fmap.ie[j].ie_chunk_size) {
				snprintf(tmpbuf, PATH_MAX - 1,
					 "ie[%d].chunk_size mismatch %lld / %lld\n", j,
					 m1->fm_fmap.ie[j].ie_chunk_size,
					 m2->fm_fmap.ie[j].ie_chunk_size);
				strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
				errs++;
			}
			errs += famfs_compare_simple_ext_list(msgbuf, m1->fm_fmap.fmap_niext,
							      m1->fm_fmap.ie[j].ie_strips,
							      m2->fm_fmap.ie[j].ie_strips);
		}
	}

	if (verbose && errs) {
		fprintf(stderr, "%s", msgbuf);
		msgbuf[0] = 0;
	}

	return errs;
}
