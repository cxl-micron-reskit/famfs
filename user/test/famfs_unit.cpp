/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2015-2021 Micron Technology, Inc.  All rights reserved.
 */

#include <gtest/gtest.h>

#define FAMFS_UNIT_TEST

extern "C" {
#include <unistd.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>


#include "famfs_lib.h"
#include "famfs_meta.h"
#include "famfs_ioctl.h"
#include "xrand.h"
#include "random_buffer.h"
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

	logp->famfs_log_magic++;
	rc = famfs_validate_log_header(logp);
	ASSERT_LT(rc, 0);

	logp->famfs_log_magic--;
	logp-> famfs_log_crc++;
	rc = famfs_validate_log_header(logp);
	ASSERT_LT(rc, 0);

	logp->famfs_log_crc--;
	rc = famfs_validate_log_header(logp);
	ASSERT_EQ(rc, 0);
}

#define SB_RELPATH ".meta/.superblock"
#define LOG_RELPATH ".meta/.log"

TEST(famfs, famfs_open_relpath)
{
	int rc;

	/* TODO: add relative path checks (getcwd(), chdir(), use rellative paths, chdir back) */

	/* /tmp/famfs should already exist and have a superblock and log in it */
	system("mkdir -p /tmp/famfs/0000/1111/2222/3333/4444/5555");

	rc = __open_relpath("/tmp/bogus/path", SB_RELPATH, 1, NULL, NULL, 1);
	ASSERT_NE(rc, 0);;

	rc = __open_relpath("/tmp/bogus/path", SB_RELPATH, 1, NULL, NULL, 1);
	ASSERT_NE(rc, 0);

	/* Good, no ascent necessary  */
	rc = __open_relpath("/tmp/famfs/", LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_GT(rc, 0);
	close(rc);
	rc = __open_relpath("/tmp/famfs", LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* Good but deep path */
	rc = __open_relpath("/tmp/famfs/0000/1111/2222/3333/4444/5555",
			    LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* Bogus path that ascends to a real path with .meta */
	rc = __open_relpath("/tmp/famfs/0000/1111/2222/3333/4444/5555/66666",
			    LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* Deep bogus path that ascends to a real path with .meta */
	rc = __open_relpath("/tmp/famfs/0000/1111/2222/3333/4444/5555/66666/7/6/5/4/3/2/xxx",
			    LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* empty path */
	rc = __open_relpath("", LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_LT(rc, 0);
	close(rc);

	/* "/" */
	rc = __open_relpath("/", LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_LT(rc, 0);
	close(rc);

	/* No "/" */
	rc = __open_relpath("blablabla", LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_LT(rc, 0);
	close(rc);
	/* No "/" and spaces */
	rc = __open_relpath("bla bla bla", LOG_RELPATH, 1, NULL, NULL, 1);
	ASSERT_LT(rc, 0);
	close(rc);
}

TEST(famfs, famfs_get_device_size)
{
	enum extent_type type;
	size_t size;
	int rc;

	rc = famfs_get_device_size("/dev/zero", &size, &type);
	ASSERT_NE(rc, 0);
	rc = famfs_get_device_size("badfile", &size, &type);
	ASSERT_NE(rc, 0);
	rc = famfs_get_device_size("/etc/hosts", &size, &type);
	ASSERT_NE(rc, 0);
}

TEST(famfs, famfs_xrand64_tls)
{
	u_int64_t num;
	struct xrand xr;

	xrand_init(&xr, 42);
	ASSERT_NE(num, 0);
	num = xrand64_tls();
	ASSERT_NE(num, 0);
	num = xrand_range64(&xr, 42, 0x100000);
	ASSERT_NE(num, 0);
}

TEST(famfs, famfs_random_buffer)
{
	struct xrand xr;
	//u_int32_t rnum;
	char buf[16];
	int rc;

	xrand_init(&xr, 42);
	randomize_buffer(buf, 0, 11);
	rc = validate_random_buffer(buf, 0, 11);
	ASSERT_EQ(rc, -1);
#if 0
	rnum = generate_random_u32(1, 10);
	ASSERT_GT(rnum, 0);
	ASSERT_LT(rnum, 11);
#endif
}

#define booboofile "/tmp/booboo"
TEST(famfs, famfs_file_not_famfs)
{
	int sfd;
	int rc;

	system("rm -rf" booboofile);
	sfd = open(booboofile, O_RDWR | O_CREAT, 0666);
	ASSERT_NE(sfd, 0);
	rc = __file_not_famfs(sfd);
	ASSERT_NE(rc, 0);
	close(sfd);

	rc = file_not_famfs(booboofile);
	ASSERT_NE(rc, 0);
}

TEST(famfs, famfs_mkmeta)
{
	int rc;

	rc = famfs_mkmeta("/dev/bogusdev");
	ASSERT_NE(rc, 0);
}

TEST(famfs, mmap_whole_file)
{
	size_t size;
	void *addr;;

	addr = famfs_mmap_whole_file("bogusfile", 1, &size);
	ASSERT_NE(addr, MAP_FAILED);
	addr = famfs_mmap_whole_file("/dev/zero", 1, &size);
	ASSERT_NE(addr, MAP_FAILED);
}

TEST(famfs, __famfs_cp)
{
	int rc;

	/* OK, this is coverage hackery. Beware */
	rc = __famfs_cp((struct famfs_locked_log *)0xdeadbeef,
			"badsrcfile",
			"xx",
			0, 0, 0, 0);
	ASSERT_EQ(rc, 1);
	rc = __famfs_cp((struct famfs_locked_log *)0xdeadbeef,
			"/etc",
			"xx",
			0, 0, 0, 0);
	ASSERT_EQ(rc, 1);
	rc = __famfs_cp((struct famfs_locked_log *)0xdeadbeef,
			"/dev/zero",
			"xx",
			0, 0, 0, 0);
	ASSERT_EQ(rc, 1);

}
