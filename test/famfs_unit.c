/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

#include <unistd.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "famfs_lib.h"
#include "famfs_lib_internal.h"
#include "famfs_meta.h"
#include "famfs_unit.h"

#define famfs_assert_eq(a, b) {						\
	if ((a) != (b))	{						\
		fprintf(stderr, "%s:%d %lld != %lld\n",			\
		   __FILE__, __LINE__, (u64)(a), (u64)(b));		\
		return __LINE__;					\
	}								\
}

#define famfs_assert_gt(a, b) {						\
	 if ((a) <= (b))	{					\
		 fprintf(stderr, "%s:%d %lld !<= %lld\n",		\
			 __FILE__, __LINE__, (u64)(a), (u64)(b));	\
		 return __LINE__;					\
	 }								\
}

#define famfs_assert_ne(a, b) {						\
	if ((a) == (b)) {						\
		fprintf(stderr, "%s:%d %lld == %lld\n",			\
			__FILE__, __LINE__, (u64)(a), (u64)(b));	\
		return __LINE__;					\
	}								\
}

int create_mock_famfs_instance(
	const char *path,
	u64 device_size,
	struct famfs_superblock **sb_out,
	struct famfs_log **log_out)
{
	char *buf  = (char *)calloc(1, FAMFS_LOG_LEN);
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	char pathbuf[PATH_MAX];
	mode_t mode = 0777;
	int lfd, sfd;
	void *addr;
	int rc;

	if (strstr(path, "/tmp/") != path) {
		/* Don't allow a unit test running as root to blow away arbitrary file systems :D */
		printf("%s: path (%s) must begin with /tmp", __func__, path);
		return -1;
	}
	snprintf(pathbuf, PATH_MAX - 1, "rm -rf %s", path);
	system(pathbuf);

	/* Create fake famfs and famfs/.meta mount point */
	rc = mkdir(path, mode);
	famfs_assert_eq(rc, 0);

	snprintf(pathbuf, PATH_MAX - 1, "%s/.meta", path);
	rc = mkdir(pathbuf, mode);
	famfs_assert_eq(rc, 0);

	/* Create fake log and superblock files */
	snprintf(pathbuf, PATH_MAX - 1, "%s/.meta/.superblock", path);
	sfd = open(pathbuf, O_RDWR | O_CREAT, 0666);
	famfs_assert_gt(sfd, 0);

	snprintf(pathbuf, PATH_MAX - 1, "%s/.meta/.log", path);
	lfd = open(pathbuf, O_RDWR | O_CREAT, 0666);
	famfs_assert_gt(lfd, 0);

	/* Zero out the superblock and log files */
	rc = write(sfd, buf, FAMFS_SUPERBLOCK_SIZE);
	famfs_assert_eq(rc, FAMFS_SUPERBLOCK_SIZE);

	rc = write(lfd, buf, FAMFS_LOG_LEN);
	famfs_assert_eq(rc, FAMFS_LOG_LEN);
	
	/* Mmap fake log file */
	addr = mmap(0, FAMFS_SUPERBLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
	famfs_assert_ne(addr, MAP_FAILED);
	sb = (struct famfs_superblock *)addr;
	*sb_out = sb;

	famfs_dump_super(sb); /* dump invalid superblock */

	/* mmap fake superblock file */
	addr = mmap(0, FAMFS_LOG_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	famfs_assert_ne(addr, MAP_FAILED);
	logp = (struct famfs_log *)addr;
	*log_out = logp;

	famfs_dump_log(logp); /* dump invalid superblock */

	memset(sb, 0, FAMFS_SUPERBLOCK_SIZE);
	memset(logp, 0, FAMFS_LOG_LEN);

	/* First mkfs should succeed */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, FAMFS_LOG_LEN, device_size, 0, 0);
	famfs_assert_eq(rc, 0);

	close(lfd);
	close(sfd);
	return 0;
}

