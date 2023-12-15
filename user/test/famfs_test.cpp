/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2015-2021 Micron Technology, Inc.  All rights reserved.
 */

#include <gtest/gtest.h>

extern "C" {
#include "famfs_lib.h"
#include "famfs_meta.h"
#include <unistd.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
}

/* These are really system test calls that need an actual famfs file system mounted
 * at /mnt/famfs
 */


/****+++++++++++++++++++++++++++++++++++++++++++++
 * NOTE THESE TESTS MUST BE RUN AS ROOT!!
 */

#define FAMFS_MPT "/mnt/famfs"
#define DIRPATH   "/mnt/famfs/testdir42"
#define TESTFILE  "/mnt/famfs/testdir42/testfile0"

TEST(famfs, dummy)
{
	printf("Dummy test\n");
	ASSERT_EQ(0, 0);
}

TEST(famfs, famfs_mkfs)
{
	char *buf  = (char *)calloc(1, FAMFS_LOG_LEN);
	u64 device_size = 1024 * 1024 * 1024;
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	mode_t mode = 0777;
	int lfd, sfd;
	void *addr;
	int rc;

	system("rm -rf /tmp/famfs");

	/* Create fake famfs and famfs/.meta mount point */
	rc = mkdir("/tmp/famfs", mode);
	ASSERT_EQ(rc, 0);
	rc = mkdir("/tmp/famfs/.meta", mode);
	ASSERT_EQ(rc, 0);

	/* Create fake log and superblock files */
	sfd = open("/tmp/famfs/.meta/.superblock", O_RDWR | O_CREAT, 0666);
	ASSERT_GT(sfd, 0);
	lfd = open("/tmp/famfs/.meta/.log", O_RDWR | O_CREAT, 0666);
	ASSERT_GT(lfd, 0);

	/* Zero out the superblock and llog files */
	rc = write(sfd, buf, FAMFS_SUPERBLOCK_SIZE);
	ASSERT_EQ(rc, FAMFS_SUPERBLOCK_SIZE);

	rc = write(lfd, buf, FAMFS_LOG_LEN);
	ASSERT_EQ(rc, FAMFS_LOG_LEN);

	/* Mmap fake log file */
	addr = mmap(0, FAMFS_SUPERBLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0);
	ASSERT_NE(addr, MAP_FAILED);
	logp = (struct famfs_log *)addr;

	/* mmap fake superblock file */
	addr = mmap(0, FAMFS_LOG_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	ASSERT_NE(addr, MAP_FAILED);
	sb = (struct famfs_superblock *)addr;

	memset(sb, 0, FAMFS_SUPERBLOCK_SIZE);
	memset(logp, 0, FAMFS_LOG_LEN);

	/* First mkfs should succeed */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, device_size, 0, 0);
	ASSERT_EQ(rc, 0);

	/* Repeat should fail because there is a valid superblock */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, device_size, 0, 0);
	ASSERT_NE(rc, 0);

	/* Repeat with kill and force should succeed */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, device_size, 1, 1);
	ASSERT_EQ(rc, 0);

	/* Repeat without force should succeed because we wiped out the old superblock */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, device_size, 0, 0);
	ASSERT_EQ(rc, 0);

	/* Repeat without force should fail because there is a valid sb again */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, device_size, 0, 0);
	ASSERT_NE(rc, 0);

	/* Repeat with force should succeed because of force */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, device_size, 1, 0);
	ASSERT_EQ(rc, 0);

	close(lfd);
	close(sfd);

	/* This leaves a valid superblock and log at /tmp/famfs/.meta ... */

}

TEST(famfs, famfs_super_test)
{
	u64 device_size = 1024 * 1024 * 1024;
	struct famfs_superblock *sb = NULL;
	struct famfs_log *logp;
	int rc;

	/* null superblock should fail */
	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, -1);

	sb = (struct famfs_superblock *)calloc(1, sizeof(*sb));
	logp = (struct famfs_log *)calloc(1, FAMFS_LOG_LEN);

	/* Make a fake file system with our fake sb and log */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, device_size, 0, 0);
	ASSERT_EQ(rc, 0);

	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, 0);

	sb->ts_magic--; /* bad magic number */
	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, -1);

	sb->ts_magic++; /* good magic number */
	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, 0);

	sb->ts_version++;  /* unrecognized version */
	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, -1);

	sb->ts_version = FAMFS_CURRENT_VERSION;  /* version good again */
	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, 0);

	sb->ts_crc++; /* bad crc */
	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, -1);

	sb->ts_crc = famfs_gen_superblock_crc(sb);
	rc = famfs_check_super(sb);
	ASSERT_EQ(rc, 0); /* good crc */

}

//MTF_END_UTEST_COLLECTION(cheap_test)
