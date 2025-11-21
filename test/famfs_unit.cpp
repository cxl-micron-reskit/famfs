// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

#include <gtest/gtest.h>

extern "C" {
#include <stdio.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>


#include <linux/famfs_ioctl.h>
#include "famfs_lib.h"
#include "famfs_lib_internal.h"
#include "famfs_meta.h"
#include "famfs_fmap.h"
#include "xrand.h"
#include "random_buffer.h"
#include "famfs_unit.h"

//#define _GNU_SOURCE
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include "famfs_fused_icache.h"
#include "famfs_fused.h"
}

/****+++++++++++++++++++++++++++++++++++++++++++++
 * NOTE THESE TESTS MUST BE RUN AS ROOT!!
 * (perhaps we'll get around to mitigaing this...)
 */

#define FAMFS_MPT "/mnt/famfs"
#define DIRPATH   "/mnt/famfs/testdir42"
#define TESTFILE  "/mnt/famfs/testdir42/testfile0"
#define PATH	  256
#define SYS_UUID_DIR "/opt/famfs"

TEST(famfs, dummy)
{
	extern int mock_fstype;

	mock_fstype = FAMFS_V1;
	printf("Dummy test\n");
	ASSERT_EQ(0, 0);
}

TEST(famfs, famfs_misc)
{
	char **strings;
	int rc;

	rc = check_file_exists("/tmp", "this-file-should-not-exist", 1,
			       0, NULL, 1);
	ASSERT_EQ(rc, -1);
	rc = famfs_flush_file("/tmp/this-file-should-not-exist", 1);
	ASSERT_EQ(rc, 3);
	free_string_list(NULL, 1);
	rc = get_multiplier(NULL);
	ASSERT_EQ(rc, 1);
	rc = get_multiplier("mm");
	ASSERT_EQ(rc, -1);
	rc = kernel_symbol_exists("fuse_file_famfs", "fuse", 1);
	EXPECT_TRUE(rc == 0 || rc == 1);
	rc = kernel_symbol_exists("famfs_create", "famfs", 1);
	EXPECT_TRUE(rc == 0 || rc == 1);
	rc = kernel_symbol_exists("famfs_create", "famfsv1", 1);
	EXPECT_TRUE(rc == 0 || rc == 1);
	rc = famfs_get_kernel_type(1);
	EXPECT_TRUE(rc == FAMFS_FUSE || rc == FAMFS_V1 || rc == NOT_FAMFS);
	strings = tokenize_string(NULL, ",", NULL);
	EXPECT_TRUE(strings == NULL);
}

TEST(famfs, famfs_create_sys_uuid_file)
{
	char sys_uuid_file[PATH];
	int rc;
	extern int mock_uuid;
	uuid_le uuid_out;


	// Check with correct file name and path
	snprintf(sys_uuid_file, PATH, "/opt/famfs/system_uuid");
	rc = famfs_create_sys_uuid_file(sys_uuid_file);
	ASSERT_EQ(rc, 0);

	// Pass a directory, should fail
	system("mkdir -p /tmp/famfs");
	snprintf(sys_uuid_file, PATH, "%s", "/tmp/famfs");
	rc = famfs_create_sys_uuid_file(sys_uuid_file);
	ASSERT_NE(rc, 0);

	// create a uuid file
	snprintf(sys_uuid_file, PATH, "/tmp/system_uuid");
	rc = famfs_create_sys_uuid_file(sys_uuid_file);
	ASSERT_EQ(rc, 0);
	system("rm /tmp/system_uuid");

	// simulate directory creation failure
	mock_uuid = 1;
	system("mv /opt/famfs /opt/famfs_old");
	snprintf(sys_uuid_file, PATH, "/opt/famfs/system_uuid");
	rc = famfs_create_sys_uuid_file(sys_uuid_file);
	ASSERT_NE(rc, 0);
	system("rmdir /opt/famfs");
	system("mv /opt/famfs_old /opt/famfs");
	mock_uuid = 0;

	// simulate write failure with mock_uuid
	mock_uuid = 1;
	snprintf(sys_uuid_file, PATH, "/tmp/system_uuid");
	rc = famfs_create_sys_uuid_file(sys_uuid_file);
	ASSERT_NE(rc, 0);

	// simulate fscanf failure in famfs_get_system_uuid
	rc = famfs_get_system_uuid(&uuid_out);
	ASSERT_NE(rc, 0);
	mock_uuid = 0;

}

TEST(famfs, famfs_mkfs)
{
	u64 device_size = 1024 * 1024 * 1024;
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	int rc;

	/* Prepare a fake famfs (move changes to this block everywhere it is) */
	rc = create_mock_famfs_instance("/tmp/famfs", device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);

	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, 0);

	/* Try a bad mkfs - invalid log length */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, 1, device_size, 0, 0);
	ASSERT_NE(rc, 0);

	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, 0);

	/* Repeat should fail because there is a valid superblock */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, FAMFS_LOG_LEN, device_size, 0, 0);
	ASSERT_NE(rc, 0);

	/* Repeat with kill and force should succeed */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, FAMFS_LOG_LEN, device_size, 1, 1);
	ASSERT_EQ(rc, 0);

	/* Repeat without force should succeed because we wiped out the old superblock */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, FAMFS_LOG_LEN, device_size, 0, 0);
	ASSERT_EQ(rc, 0);

	/* Repeat without force should fail because there is a valid sb again */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, FAMFS_LOG_LEN, device_size, 0, 0);
	ASSERT_NE(rc, 0);

	/* Repeat with force should succeed because of force */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, FAMFS_LOG_LEN, device_size, 1, 0);
	ASSERT_EQ(rc, 0);

	/* This leaves a valid superblock and log at /tmp/famfs/.meta ... */

}

TEST(famfs, famfs_super_test)
{
	u64 device_size = 1024 * 1024 * 1024;
	struct famfs_superblock *sb = NULL;
	struct famfs_log *logp;
	extern int mock_flush;
	int rc;

	mock_flush = 1;

	/* null superblock should fail */
	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, -1);

	sb = (struct famfs_superblock *)calloc(1, sizeof(*sb));
	logp = (struct famfs_log *)calloc(1, FAMFS_LOG_LEN);

	/* Make a fake file system with our fake sb and log */
	rc = __famfs_mkfs("/dev/dax0.0", sb, logp, FAMFS_LOG_LEN, device_size, 0, 0);
	ASSERT_EQ(rc, 0);

	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, 0);

	sb->ts_magic--; /* bad magic number */
	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, -1);

	sb->ts_magic++; /* good magic number */
	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, 0);

	sb->ts_version++;  /* unrecognized version */
	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, 1); /* new: bad version returns 1 - distinguishable */

	sb->ts_version = FAMFS_CURRENT_VERSION;  /* version good again */
	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, 0);

	sb->ts_crc++; /* bad crc */
	rc = famfs_check_super(sb, NULL, NULL);
	ASSERT_EQ(rc, -1);

	sb->ts_crc = famfs_gen_superblock_crc(sb);
	rc = famfs_check_super(sb, NULL, NULL);
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

	rc = __open_relpath("/tmp/bogus/path", SB_RELPATH, 1, NULL, -1,
			    NULL, NO_LOCK, 1);
	ASSERT_NE(rc, 0);;

	rc = __open_relpath("/tmp/bogus/path", SB_RELPATH, 1, NULL, -1,
			    NULL, NO_LOCK, 1);
	ASSERT_NE(rc, 0);

	/* Good, no ascent necessary  */
	rc = __open_relpath("/tmp/famfs/", LOG_RELPATH, 1, NULL, -1,
			    NULL, NO_LOCK, 1);
	ASSERT_GT(rc, 0);
	close(rc);
	rc = __open_relpath("/tmp/famfs", LOG_RELPATH, 1, NULL, -1,
			    NULL, NO_LOCK, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* Good but deep path */
	rc = __open_relpath("/tmp/famfs/0000/1111/2222/3333/4444/5555",
			    LOG_RELPATH, 1, NULL, -1, NULL, NO_LOCK, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* Bogus path that ascends to a real path with .meta */
	rc = __open_relpath("/tmp/famfs/0000/1111/2222/3333/4444/5555/66666",
			    LOG_RELPATH, 1, NULL, -1, NULL, NO_LOCK, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* Deep bogus path that ascends to a real path with .meta */
	rc = __open_relpath("/tmp/famfs/0000/1111/2222/3333/4444/5555/66666/7/6/5/4/3/2/xxx",
			    LOG_RELPATH, 1, NULL, -1, NULL, NO_LOCK, 1);
	ASSERT_GT(rc, 0);
	close(rc);

	/* empty path */
	rc = __open_relpath("", LOG_RELPATH, 1, NULL, -1, NULL, NO_LOCK, 1);
	ASSERT_LT(rc, 0);
	close(rc);

	/* "/" */
	rc = __open_relpath("/", LOG_RELPATH, 1, NULL, -1, NULL, NO_LOCK, 1);
	ASSERT_LT(rc, 0);
	close(rc);

	/* No "/" */
	rc = __open_relpath("blablabla", LOG_RELPATH, 1, NULL, -1, NULL, BLOCKING_LOCK, 1);
	ASSERT_LT(rc, 0);
	close(rc);
	/* No "/" and spaces */
	rc = __open_relpath("bla bla bla", LOG_RELPATH, 1, NULL, -1, NULL, NON_BLOCKING_LOCK, 1);
	ASSERT_LT(rc, 0);
	close(rc);
}

TEST(famfs, famfs_get_device_size)
{
	size_t size;
	int rc;

	rc = famfs_get_device_size("/dev/zero", &size, 0);
	ASSERT_NE(rc, 0);
	rc = famfs_get_device_size("badfile", &size, 1);
	ASSERT_NE(rc, 0);
	rc = famfs_get_device_size("/etc/hosts", &size, 0);
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
TEST(famfs, famfs_file_is_famfs_v1)
{
	int sfd;
	int rc;
	extern int mock_kmod;
	int mock_kmod_save = mock_kmod;

	system("rm -rf " booboofile);
	sfd = open(booboofile, O_RDWR | O_CREAT, 0666);
	ASSERT_NE(sfd, 0);

	mock_kmod = 0;
	rc = __file_is_famfs_v1(sfd);
	ASSERT_EQ(rc, 0);
	mock_kmod = mock_kmod_save;
	close(sfd);

	rc = file_is_famfs_v1(booboofile);
	ASSERT_EQ(rc, 0);

	rc = file_is_famfs_v1("/tmp/non-existent-file");
	ASSERT_EQ(rc, 0);
}

TEST(famfs, famfs_mkmeta)
{
	int rc;

	rc = famfs_mkmeta_standalone("/dev/bogusdev", 1);
	ASSERT_NE(rc, 0);
}

TEST(famfs, mmap_whole_file)
{
	size_t size;
	void *addr;
	int sfd;

	addr = famfs_mmap_whole_file("bogusfile", 1, &size);
	ASSERT_NE(addr, MAP_FAILED);
	addr = famfs_mmap_whole_file("/dev/zero", 1, &size);
	ASSERT_NE(addr, MAP_FAILED);

	sfd = open("/tmp/famfs/frab", O_RDWR | O_CREAT, 0666); /* make empty file */
	ASSERT_GT(sfd, 0);
	close(sfd);
	addr = famfs_mmap_whole_file("/tmp/famfs/frab", 1, 0); /* empty file */
	ASSERT_EQ((long long)addr, 0);
}

TEST(famfs, __famfs_cp)
{
	u64 device_size = 1024 * 1024 * 256;
	struct famfs_locked_log ll;
	struct famfs_superblock *sb;
	extern int mock_failure;
	struct famfs_log *logp;
	extern int mock_kmod, mock_fstype;
	int rc;

	/* Prepare a fake famfs  */
	mock_kmod = 1;
	mock_fstype = FAMFS_V1;
	rc = create_mock_famfs_instance("/tmp/famfs", device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);
	rc = famfs_init_locked_log(&ll, "/tmp/famfs", 0, 1);
	ASSERT_EQ(rc, 0);
	mock_kmod = 0;

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


	/* exercise verbose path */
	system("touch /tmp/src");
	rc = __famfs_cp(&ll,
			"/tmp/src",
			"xx",
			0, 0, 0, 2);
	ASSERT_NE(rc, 0);
	system("rm /tmp/src");

	/* fail open of src file */
	system("dd if=/dev/random of=/tmp/src bs=4096 count=1");
	mock_failure = MOCK_FAIL_OPEN;
	rc = __famfs_cp(&ll,
			"/tmp/src",
			"xx",
			0, 0, 0, 2);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;
	system("rm /tmp/src");

	/* fail fd of dest file */
	system("dd if=/dev/random of=/tmp/src bs=4096 count=1");
	rc = __famfs_cp(&ll,
			"/tmp/src",
			"/tmp/dest",
			0, 0, 0, 2);
	system("rm /tmp/src");
	ASSERT_NE(rc, 0);

	/* fail mmap of dest file*/
	system("dd if=/dev/random of=/tmp/src bs=4096 count=1");
	mock_kmod = 1;
	mock_failure = MOCK_FAIL_MMAP;
	rc = __famfs_cp(&ll,
			"/tmp/src",
			"/tmp/famfs/dest",
			0, 0, 0, 2);
	system("rm /tmp/src");
	mock_failure = MOCK_FAIL_NONE;
	mock_kmod = 0;
	ASSERT_NE(rc, 0);

	/* fail srcfile read */
	system("dd if=/dev/random of=/tmp/src bs=4096 count=1");
	mock_kmod = 1;
	rc = __famfs_cp(&ll,
			"/tmp/src",
			"/tmp/famfs/dest",
			0, 0, 0, 2);
	system("rm /tmp/src");
	mock_kmod = 0;
	ASSERT_NE(rc, 0);
}

TEST(famfs, famfs_alloc)
{
	u64 device_size = 1024 * 1024 * 256;
	struct famfs_log_fmap *fmap = NULL;
	struct famfs_superblock *sb;
	struct famfs_locked_log ll;
	char *fspath = "/tmp/famfs";
	struct famfs_log *logp;
	extern int mock_kmod;
	extern int mock_fstype;
	int rc;

	/* Prepare a fake famfs  */
	mock_kmod = 1;
	mock_fstype = FAMFS_V1;
	rc = create_mock_famfs_instance(fspath, device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);
	rc = famfs_init_locked_log(&ll, fspath, 0, 1);
	ASSERT_EQ(rc, 0);
	mock_kmod = 0;

	rc = famfs_file_alloc(&ll, 4096, &fmap, 1);
	ASSERT_EQ(rc, 0);

	printf("locked_log: devsize %lld/0x%llx, nbits %lld\n",
	       ll.devsize, ll.devsize, ll.nbits);

	mu_print_bitmap(ll.bitmap, ll.nbits);
	ASSERT_NE(fmap, nullptr);
	ASSERT_EQ(fmap->fmap_ext_type, FAMFS_EXT_SIMPLE);
	ASSERT_EQ(fmap->fmap_nextents, 1);
	ASSERT_NE(fmap->se[0].se_offset, 0);

#if (FAMFS_KABI_VERSION > 42)
	char bro_path[PATH_MAX];
	int fd;

#define MiB 0x100000

	/*
	 * Test stripe validation
	 */
	/* Bad chunk_size */
	ll.interleave_param.nbuckets = 8;
	ll.interleave_param.nstrips = 8;
	ll.interleave_param.chunk_size = 0;
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/* more strips than buckets */
	ll.interleave_param.nbuckets = 8;
	ll.interleave_param.nstrips = 6;
	ll.interleave_param.chunk_size = 2 * MiB;
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/* chunk_size not multiple of alloc unit */
	ll.interleave_param.nbuckets = 8;
	ll.interleave_param.nstrips = 6;
	ll.interleave_param.chunk_size = 1 * MiB;
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/* non-power-of-2 chunk */
	ll.interleave_param.nbuckets = 8;
	ll.interleave_param.nstrips = 8;
	ll.interleave_param.chunk_size = 2 * MiB + 1;
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/* Too many buckets */
	ll.interleave_param.nbuckets = FAMFS_MAX_NBUCKETS + 2;
	ll.interleave_param.nstrips = 6;
	ll.interleave_param.chunk_size = 2 * MiB;
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/*********************/

	/*
	 * Test actual stripe allocation
	 */
	/* Now set up for striped allocation */
	ll.interleave_param.nbuckets = 8; /* each bucket is 32MiB */
	ll.interleave_param.nstrips = 8;
	ll.interleave_param.chunk_size = 2 * MiB;
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2); /* This should fit */
	ASSERT_EQ(rc, 1);

	extern int mock_stripe;
	mock_stripe = 1;
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2); /* This should fit */
	ASSERT_EQ(rc, 0);
	ASSERT_NE(fmap, nullptr);
	ASSERT_EQ(fmap->fmap_ext_type, FAMFS_EXT_INTERLEAVE);
	ASSERT_EQ(fmap->fmap_nextents, 1);
	ASSERT_EQ(fmap->ie[0].ie_nstrips, ll.interleave_param.nstrips);
	ASSERT_EQ(fmap->ie[0].ie_chunk_size, ll.interleave_param.chunk_size);
	ASSERT_EQ(fmap->ie[0].ie_nstrips, 8);

	/* A second allocation of the same size should fail on the first strip,
	 * because the superblock and log are there */
	rc = famfs_file_alloc(&ll, 8 * 16 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/* Allocating a small file should be non-strided if size < chunk_size */
	rc = famfs_file_alloc(&ll, 4096, &fmap, 1);
	ASSERT_EQ(rc, 0);

	/* Chunk size must be a multiple of FAMFS_ALLOC_UNIT, so
	 * this should fail */
	ll.interleave_param.chunk_size += 1;

	/* But small alloc should still succeed because it won't be strided */
	rc = famfs_file_alloc(&ll, 4 * MiB, &fmap, 1);
	ASSERT_NE(rc, 0);

	ll.interleave_param.chunk_size--; /* make it valid again */
	ll.interleave_param.nstrips = 6;  /* Fewer strips; try an alloc that
					   * not all strips can handle,
					   * but enough can */

	printf("1:\n");
	rc = famfs_file_alloc(&ll, 16 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);

	printf("2:\n");
	rc = famfs_file_alloc(&ll, 16 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);

	printf("3:\n");
	rc = famfs_file_alloc(&ll, 16 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);

	printf("4:\n");
	rc = famfs_file_alloc(&ll, 16 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);

	printf("5:\n");
	rc = famfs_file_alloc(&ll, 16 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/* There should only be 9 extents left. Do an alloc that should get it */
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_EQ(rc, 0);

	mu_print_bitmap(ll.bitmap, ll.nbits);

	/* additional allocations should fail no matter what */
	rc = famfs_file_alloc(&ll, 1 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);
	rc = famfs_file_alloc(&ll, 1, &fmap, 2);
	ASSERT_NE(rc, 0);
	rc = famfs_file_alloc(&ll, 100 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);
	rc = famfs_file_alloc(&ll, 1000 * MiB, &fmap, 2);
	ASSERT_NE(rc, 0);

	/* Blow away and re-create the mock famfs instance */
	mock_kmod = 1;
	rc = create_mock_famfs_instance(fspath, device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);
	rc = famfs_init_locked_log(&ll, fspath, 0, 1);
	ASSERT_EQ(rc, 0);

	sprintf(bro_path, "%s/non-interleaved-file", fspath);
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 2097152, 0, 1);
	ASSERT_GT(fd, 0);

	/* recreate same file should fail */
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 2097152, 0, 1);
	ASSERT_LT(fd, 0);

#if 0
	/* XXX these unit test files seem to be created with zero size,
	 * so this test doesn't work; should look into this...
	 */
	/* recreate same file should succeed with 'open_existing' if the
	 * size matches
	 */
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 2097152,
			    1, /* open_existing */
			    1);
	ASSERT_GT(fd, 0);
#endif
	/* recreate same file should fail with 'open_existing' if the
	 * size is a mismatch
	 */
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 100,
			    1, /* open_existing */
			    1);
	ASSERT_LT(fd, 0);

	/* Now set up for striped allocation */
	ll.interleave_param.nbuckets = 8; /* each bucket is 32MiB */
	ll.interleave_param.nstrips = 8;
	ll.interleave_param.chunk_size = 2 * MiB;

	/* This allocation will fall back to non-interleaved because it's too small */
	sprintf(bro_path, "%s/non-interleaved_file", fspath);
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 2097152, 0, 1);
	ASSERT_GT(fd, NULL);
	close(fd);

	/* This allocation will be interleaved */
	sprintf(bro_path, "%s/fallback-file0", fspath);
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 32 * MiB, 0, 1);
	ASSERT_GT(fd, 0);
	close(fd);

	/* This allocation will be interleaved with space amplification */
	sprintf(bro_path, "%s/interleaved-file0", fspath);
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 3 * MiB, 0, 1);
	ASSERT_GT(fd, 0);

	/* This allocation will be interleaved with a bit less space amp */
	sprintf(bro_path, "%s/interleaved-file1", fspath);
	fd = __famfs_mkfile(&ll, bro_path, 0, 0, 0, 8 * MiB, 0, 1);
	ASSERT_GT(fd, 0);
	close(fd);

	/* Do a dry_run shadow log play */
	rc = __famfs_logplay(fspath, ll.logp,
			     1 /* dry_run */,
			     1 /* shadow */,
			     1 /* shadowtest */,
			     FAMFS_MASTER,
			     1 /* verbose */);
	ASSERT_EQ(rc, 0);

	/* Do a full shadow log play */
	rc = __famfs_logplay(fspath, ll.logp,
			     1 /* dry_run */,
			     1 /* shadow */,
			     1 /* shadowtest */,
			     FAMFS_MASTER,
			     1 /* verbose */);
	ASSERT_EQ(rc, 0);

	/* Build the bitmap from the log and compare to the bitmap from creating files
	 * This tests the processing of log entries (inc. the currently-new striped entries
	 */
	u64 nbits, alloc_errs, fsize_total, alloc_sum;
	struct famfs_log_stats logstats;
	memset(&logstats, 0, sizeof(logstats));
	u64 nbytes;
	u8 *bitmap = famfs_build_bitmap(ll.logp, ll.alloc_unit, ll.devsize,
				       &nbits, &alloc_errs, &fsize_total, &alloc_sum,
				       &logstats, 1);
	ASSERT_NE(bitmap, nullptr);
	ASSERT_GT(nbits, 0);
	nbytes = (nbits + 8 - 1) / 8;
	rc = memcmp(bitmap, ll.bitmap, nbytes);
	ASSERT_EQ(rc, 0);
#endif

	mock_kmod = 0;
}

TEST(famfs, famfs_log)
{
	u64 device_size = 1024 * 1024 * 1024;
	struct famfs_superblock *sb;
	struct famfs_locked_log ll;
	extern int mock_failure;
	struct famfs_log *logp;
	extern int mock_kmod;
	extern int mock_role;
	extern int mock_path;
	extern int mock_fstype;
	u64 tmp;
	int rc;
	int i;

	mock_kmod = 1;
	mock_fstype = FAMFS_V1;
	/** can call famfs_file_alloc() and __famfs_mkdir() on our fake famfs in /tmp/famfs */

	/* Prepare a fake famfs (move changes to this block everywhere it is) */
	rc = create_mock_famfs_instance("/tmp/famfs", device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);

	rc = famfs_init_locked_log(&ll, "/tmp/famfs", 0, 1);
	ASSERT_EQ(rc, 0);

	for (i = 0; i < 512; i++) {
		char filename[64];
		int fd;
		sprintf(filename, "/tmp/famfs/%04d", i);
		fd = __famfs_mkfile(&ll, filename, 0, 0, 0, 1048576, 0, 0);
		if (i < 507)
			ASSERT_GT(fd, 0);
		else
			ASSERT_LT(fd, 0); /* out of space */
		close(fd);
	}

	/* Although we're out of memory space, we can still make directories */
	for (i = 0; i < 100; i++) {
		char dirname[64];
		sprintf(dirname, "/tmp/famfs/dir%04d", i);
		rc = __famfs_mkdir(&ll, dirname, 0, 0, 0, 0);
		ASSERT_EQ(rc, 0);
	}
	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0, FAMFS_MASTER, 3);
	ASSERT_EQ(rc, 0);

	/*
	 * Test famfs_dax_shadow_logplay:
	 * We can test arg errors here, but not acutal logplay becuase it will
	 * open the log from a dax device
	 */
	/* This should fail due to null daxdev */
	system("rm -rf /tmp/famfs_shadow");
	rc = famfs_dax_shadow_logplay("/tmp/famfs_shadow", 0, 0, NULL, 1, 0);
	ASSERT_NE(rc, 0);

	/* This should fail due to bogus daxdev, but create /tmp/famfs_shadow */
	rc = famfs_dax_shadow_logplay("/tmp/famfs_shadow", 0, 0, "/dev/bogo_dax", 1, 0);
	ASSERT_NE(rc, 0);

	/* This should fail due to bogus daxdev (but /tmp/famfs_shadow will be there already) */
	rc = famfs_dax_shadow_logplay("/tmp/famfs_shadow", 0, 0, "/dev/bogo_dax", 1, 0);
	ASSERT_NE(rc, 0);

	/* This should fail due to shadow fs path being a file and not a directory */
	system("rm -rf /tmp/famfs_shadow");
	system("touch /tmp/famfs_shadow"); /* create file where shadow dir should be */
	rc = famfs_dax_shadow_logplay("/tmp/famfs_shadow", 0, 0,
				      "/dev/bogo_dax", 1, 0);
	ASSERT_NE(rc, 0);
	system("rm -f /tmp/famfs_shadow");

	/* This should fail daxdev being bogus */
	system("mkdir -p /tmp/famfs_shadow/root");
	rc = famfs_dax_shadow_logplay("/tmp/famfs_shadow", 0, 0,
				      "/dev/bogo_dax", 1, 0);
	ASSERT_NE(rc, 0);

	/*
	 * Test shadow logplay with mocked logp
	 */
	/* Do a dry_run shadow log play */
	rc = __famfs_logplay("/tmp/famfs_shadow", logp,
			     1 /* dry_run */,
			     1 /* shadow */,
			     0 /* shadowtest */,
			     FAMFS_MASTER,
			     1 /* verbose */);
	ASSERT_EQ(rc, 0);


	printf("\nStart mark\n");
	system("sudo rm -rf /tmp/famfs_shadow2");
	system("sudo mkdir -p /tmp/famfs_shadow2/root");
	/* Do a shadow log play; shadow==2 will cause the yaml to be
	 * re-parsed and verified */
	rc = __famfs_logplay("/tmp/famfs_shadow2", logp, 0 /* dry_run */,
			     1 /* shadow */, 1 /* shadowtest */,
			     FAMFS_MASTER, 1);
	ASSERT_EQ(rc, 0);

	/* Re-do shadow logplay when the files already exist */
	/* Do a shadow log play; shadow==2 will cause the yaml to be
	 * re-parsed and verified */
	rc = __famfs_logplay("/tmp/famfs_shadow2", logp, 1 /* dry_run */,
			     1 /* shadow */, 1 /* shadowtest */,
			     FAMFS_MASTER, 1);
	ASSERT_EQ(rc, 0);

	/*
	 * Test some errors in the log header and log entries
	 */
	/* fail FAMFS_LOG_MAGIC check */
	logp->famfs_log_magic = 420;
	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0,
			     FAMFS_MASTER, 4);
	ASSERT_NE(rc, 0);
	logp->famfs_log_magic = FAMFS_LOG_MAGIC;

	/* fail famfs_validate_log_entry() */
	tmp = logp->entries[0].famfs_log_entry_seqnum;
	logp->entries[0].famfs_log_entry_seqnum = 420;
	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0,
			     FAMFS_MASTER, 4);
	ASSERT_NE(rc, 0);
	logp->entries[0].famfs_log_entry_seqnum = tmp;

	/* fail famfs_log_entry_fc_path_is_relative */
	mock_path = 1;
	tmp = logp->entries[0].famfs_log_entry_type;
	logp->entries[0].famfs_log_entry_type = FAMFS_LOG_FILE;
	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0,
			     FAMFS_MASTER, 0);
	ASSERT_NE(rc, 0);
	mock_path = 0;
	logp->entries[0].famfs_log_entry_type = tmp;

	/* reach FAMFS_LOG_INVALID */
	mock_failure = MOCK_FAIL_GENERIC;
	tmp = logp->entries[0].famfs_log_entry_type;
	logp->entries[0].famfs_log_entry_type = FAMFS_LOG_INVALID;
	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0,
			     FAMFS_MASTER, 1);
	ASSERT_EQ(rc, 0);
	mock_failure = MOCK_FAIL_NONE;
	logp->entries[0].famfs_log_entry_type = tmp;


	/* fail famfs_log_entry_md_path_is_relative for FAMFS_LOG_MKDIR */
	mock_failure = MOCK_FAIL_LOG_MKDIR;
	tmp = logp->entries[0].famfs_log_entry_type;
	logp->entries[0].famfs_log_entry_type = FAMFS_LOG_MKDIR;
	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0,
			     FAMFS_MASTER, 0);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;
	logp->entries[0].famfs_log_entry_type = tmp;

	rc = famfs_fsck_scan(sb, logp, 1, 0, 3);
	ASSERT_EQ(rc, 0);

	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			1 /* mmap */, 1, 0, 0, 1);
	ASSERT_EQ(rc, 0);
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_EQ(rc, 0);
	rc = famfs_fsck("/tmp/nonexistent-file", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);

	/* Save good copies of the log and superblock */
	system("cp /tmp/famfs/.meta/.log /tmp/famfs/.meta/.log.save");
	system("cp /tmp/famfs/.meta/.superblock /tmp/famfs/.meta/.superblock.save");

	truncate("/tmp/famfs/.meta/.superblock", 8192);
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0); /* Superblock file is short; this should fail */

	truncate("/tmp/famfs/.meta/.superblock", 7);
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);

	truncate("/tmp/famfs/.meta/.log", 8192);
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);

	unlink("/tmp/famfs/.meta/.log");
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			1 /* mmap */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	unlink("/tmp/famfs/.meta/.superblock");
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			1 /* mmap */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);

	system("chmod 200 /tmp/famfs/.meta/.log");
	rc = famfs_fsck("/tmp/famfs/.meta/.log", false /* !nodax */,
			1 /* mmap */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	rc = famfs_fsck("/tmp/famfs/.meta/.log", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);

	system("chmod 200 /tmp/famfs/.meta/.superblock");
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			1 /* mmap */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);

	system("cp /tmp/famfs/.meta/.log.save /tmp/famfs/.meta/.log");
	system("cp /tmp/famfs/.meta/.superblock.save /tmp/famfs/.meta/.superblock");

	rc = famfs_release_locked_log(&ll, 0, 0);
	ASSERT_EQ(rc, 0);

	system("chmod 444 /tmp/famfs/.meta/.log"); /* log file not writable */

	mock_role = FAMFS_CLIENT;
	rc = famfs_init_locked_log(&ll, "/tmp/famfs", 0, 1);
	ASSERT_NE(rc, 0);

	mock_role = FAMFS_CLIENT;
	rc = famfs_init_locked_log(&ll, "/tmp/famfs", 0, 1);
	ASSERT_NE(rc, 0); /* init_locked_log should fail as client */

	mock_role = 0;

	mock_failure = MOCK_FAIL_OPEN_SB;
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	mock_failure = MOCK_FAIL_READ_SB;
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	mock_failure = MOCK_FAIL_OPEN_LOG;
	rc = famfs_fsck("/tmp/famfs/.meta/.log", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	mock_failure = MOCK_FAIL_READ_LOG;
	rc = famfs_fsck("/tmp/famfs/.meta/.log", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	mock_failure = MOCK_FAIL_READ_FULL_LOG;
	rc = famfs_fsck("/tmp/famfs/.meta/.log", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	/* create a invalide block device to fail _get_Device_size*/
	system("mknod -m 200 /tmp/testblock b 3 3");
	rc = famfs_fsck("/tmp/testblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	system("rm /tmp/testblock");

	/* create a non-reg, non-block, non char device, i.e. pipe device*/
	system("mknod -m 200 /tmp/testpipe p");
	rc = famfs_fsck("/tmp/testpipe", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_NE(rc, 0);
	system("rm /tmp/testpipe");

}

TEST(famfs, famfs_log_overflow_mkdir_p)
{
	u64 device_size = 64ULL * 1024ULL * 1024ULL * 1024ULL;
	//struct famfs_locked_log ll;
	struct famfs_superblock *sb;
	char dirname[PATH_MAX];
	struct famfs_log *logp;
	extern int mock_kmod;
	int rc;
	int i;

	mock_kmod = 1;
	/** can call famfs_file_alloc() and __famfs_mkdir() on our fake famfs in /tmp/famfs */

	/* Prepare a fake famfs (move changes to this block everywhere it is) */
	rc = create_mock_famfs_instance("/tmp/famfs", device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);

	/* TODO: nested dirs and files to fill up the log */
	for (i = 0; ; i++) {
		s64 nslots = log_slots_available(logp);

		sprintf(dirname, "/tmp/famfs/dir%04d/a/b/c/d/e/f/g/h/i", i);
		/* mkdir -p */
		rc = famfs_mkdir_parents(dirname, 0644, 0, 0, (i < 2500) ? 0:2);

		if (nslots >= 10) {
			if (rc != 0)
				printf("nslots: %lld\n", nslots);
			ASSERT_EQ(rc, 0);
		} else {
			printf("nslots: %lld\n", nslots);
			ASSERT_NE(rc, 0);
			break;
		}
	}

	/* Let's check how many log entries are left */
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_EQ(rc, 0);

	famfs_dump_log(logp);

	/* Let's check how many log entries are left */
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_EQ(rc, 0);

	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0, FAMFS_MASTER, 0);
	ASSERT_EQ(rc, 0);
	//famfs_print_log_stats("famfs_log test", )

	rc = famfs_fsck_scan(sb, logp, 1, 0, 0);
	ASSERT_EQ(rc, 0);
}

TEST(famfs, famfs_clone) {
	u64 device_size = 1024 * 1024 * 256;
	struct famfs_locked_log ll;
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	extern int mock_failure;
	extern int mock_role;
	extern int mock_kmod;
	char filename[PATH_MAX * 2];
	int fd;
	int rc = 0;

	/* Prepare a fake famfs  */
	mock_kmod = 1;
	rc = create_mock_famfs_instance("/tmp/famfs", device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);
	rc = famfs_init_locked_log(&ll, "/tmp/famfs", 0, 1);
	ASSERT_EQ(rc, 0);
	sprintf(filename, "/tmp/famfs/clonesrc");
	fd = __famfs_mkfile(&ll, filename, 0, 0, 0, 2097152, 0, 1);
	ASSERT_GT(fd, 0);
	mock_kmod = 0;

	/* clone a nonexistant srcfile and fail*/
	rc = famfs_clone("/tmp/nonexistant", "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);

	/* clone existing file but not in famfs and fail */
	system("touch /tmp/randfile");
	rc = famfs_clone("/tmp/randfile", "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);

	mock_kmod = 1; /* this is needed to show srcfile as in fake famfs */
	/* fail to stat srcfile */
	mock_failure = MOCK_FAIL_GENERIC;
	rc = famfs_clone(filename, "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	/* fail to check role srcfile */
	mock_failure = MOCK_FAIL_SROLE;
	rc = famfs_clone(filename, "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	/* fail to check role destfile */
	rc = famfs_clone(filename, "/tmp/famfs1/f1");
	ASSERT_NE(rc, 0);

	/* fail to check srcfile and destfile in same FS */
	mock_failure = MOCK_FAIL_ROLE;
	rc = famfs_clone(filename, "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	/* fail to create file in client role */
	mock_role = FAMFS_CLIENT;
	rc = famfs_clone(filename, "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);
	mock_role = 0;

	/* fail to open srcfile */
	mock_failure = MOCK_FAIL_OPEN;
	rc = famfs_clone(filename, "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	/* fail to do MAP_GET ioctl */
	rc = famfs_clone(filename, "/tmp/famfs/f1");
	ASSERT_NE(rc, 0);
}

TEST(famfs, famfs_log_overflow_files)
{
	u64 device_size = 64ULL * 1024ULL * 1024ULL * 1024ULL;
	struct famfs_superblock *sb;
	char dirname[PATH_MAX];
	char filename[PATH_MAX * 2];
	struct famfs_log *logp;
	extern int mock_kmod;
	int fd;
	int rc;
	int i;

	mock_kmod = 1;
	/** can call famfs_file_alloc() and __famfs_mkdir() on our fake famfs in /tmp/famfs */

	/* Prepare a fake famfs (move changes to this block everywhere it is) */
	rc = create_mock_famfs_instance("/tmp/famfs", device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);

	/* Keep doing "mkdir -p" until the log is almost full.
	 * Each of these commands will use 10 log entries.
	 */
	for (i = 0; ; i++) {
		sprintf(dirname, "/tmp/famfs/dir%04d/a/b/c/d/e/f/g/h/i", i);
		/* mkdir -p */
		rc = famfs_mkdir_parents(dirname, 0644, 0, 0, (i < 2500) ? 0:2);
		ASSERT_EQ(rc, 0);

		sprintf(filename, "%s/%04d", dirname, i);
		fd = famfs_mkfile(filename, 0, 0, 0, 1048576, NULL, 0);
		ASSERT_GT(fd, 0);

		close(fd);

		/* When we're close to full, break and create files */
		if (log_slots_available(logp) < 12)
			break;
	}

	for (i = 0 ; ; i++) {
		printf("xyi: %d\n", i);
		sprintf(filename, "%s/%04d", dirname, i);
		fd = famfs_mkfile(filename, 0, 0, 0, 1048576, NULL, 0);
		if (log_slots_available(logp) > 0) {
			ASSERT_GT(fd, 0);
			close(fd);
		} else if (log_slots_available(logp) == 0) {
			fd = famfs_mkfile(filename, 0, 0, 0, 1048576, NULL, 0);
			ASSERT_LT(fd, 0);
			break;
		}
	}

	/* Let's check how many log entries are left */
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_EQ(rc, 0);

	famfs_dump_log(logp);

	/* Let's check how many log entries are left */
	rc = famfs_fsck("/tmp/famfs/.meta/.superblock", false /* !nodax */,
			0 /* read */, 1, 0, 0, 1);
	ASSERT_EQ(rc, 0);

	rc = __famfs_logplay("/tmp/famfs", logp, 0, 0, 0, FAMFS_MASTER, 0);
	ASSERT_EQ(rc, 0);

	rc = famfs_fsck_scan(sb, logp, 1, 0, 3);
	ASSERT_EQ(rc, 0);
}

TEST(famfs, famfs_cp) {
	u64 device_size = 1024 * 1024 * 256;
	struct famfs_locked_log ll;
	struct famfs_superblock *sb;
	struct famfs_log *logp;
	extern int mock_failure;
	extern int mock_kmod;
	char src[PATH_MAX * 2];
	char dest[PATH_MAX * 2];
	int rc = 0;

	/* Prepare a fake famfs  */
	mock_kmod = 1;
	rc = create_mock_famfs_instance("/tmp/famfs", device_size, &sb, &logp);
	ASSERT_EQ(rc, 0);
	rc = famfs_init_locked_log(&ll, "/tmp/famfs", 0, 1);
	ASSERT_EQ(rc, 0);
	mock_kmod = 0;

	system("mkdir -p /tmp/destdir");
	sprintf(dest, "/tmp/destdir");
	sprintf(src, "/tmp/src");
	rc = famfs_cp(&ll, src, dest, 0, 0, 0, 1);
	ASSERT_NE(rc, 0);

	system("touch /tmp/dest");
	sprintf(dest, "/tmp/dest");
	rc = famfs_cp(&ll, src, dest, 0, 0, 0, 1);
	ASSERT_NE(rc, 0);

	sprintf(dest, "/tmp/destdir");
	mock_failure = MOCK_FAIL_GENERIC;
	rc = famfs_cp(&ll, src, dest, 0, 0, 0, 1);
	ASSERT_NE(rc, 0);
	mock_failure = MOCK_FAIL_NONE;

	system("rm /tmp/dest");
	system("rmdir /tmp/destdir");
}

TEST(famfs, famfs_print_role_string) {
	/* Increase code coverage */
	famfs_print_role_string(FAMFS_MASTER);
	famfs_print_role_string(FAMFS_CLIENT);
	famfs_print_role_string(FAMFS_NOSUPER);
}

static int truncate_stream(FILE *stream, off_t length) {
    int fd = fileno(stream); // Obtain the file descriptor
    if (fd == -1) {
        perror("fileno");
        return -1;
    }

    if (ftruncate(fd, length) == -1) {
        perror("ftruncate");
        return -1;
    }

    return 0;
}

static void famfs_yaml_test_reset(struct famfs_log_file_meta *fm, FILE *fp, char *yaml_str)
{
	memset(fm, 0, sizeof(*fm));
	rewind(fp);
	truncate_stream(fp, 0);
	fprintf(fp, "%s", yaml_str);
	rewind(fp);
}

TEST(famfs, famfs_file_yaml) {
	//struct famfs_stripe stripe;
	struct famfs_log_file_meta fm;
	FILE *fp = tmpfile();
	char *my_yaml;
	int rc;

	/* Good yaml, single extent */
	my_yaml = "---\n" /* Good yaml */
		"file:\n"
		"  path: 0446\n"
		"  size: 1048576\n"
		"  flags: 2\n"
		"  mode: 0644\n"
		"  uid: 42\n"
		"  gid: 42\n"
		"  nextents: 1\n"
		"  simple_ext_list:\n"
		"  - offset: 0x38600000\n"
		"    length: 0x200000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 1, FAMFS_MAX_SIMPLE_EXTENTS, 2);
	ASSERT_EQ(rc, 0);

	/* Good yaml, single extent */
	my_yaml = "---\n" /* Good yaml */
		"file:\n"
		"  path: 0446\n"
		"  size: 1048576\n"
		"  flags: 2\n"
		"  mode: 0644\n"
		"  uid: 42\n"
		"  gid: 42\n"
		"  badkey: foobar\n" /* Unrecognized key */
		"  nextents: 1\n"
		"  simple_ext_list:\n"
		"  - offset: 0x38600000\n"
		"    length: 0x200000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 1, FAMFS_MAX_SIMPLE_EXTENTS, 2);
	ASSERT_EQ(rc, -EINVAL);

	/* Good yaml, 3 extents */
	my_yaml = "---\n" /* Good yaml */
		"file:\n"
		"  path: 0446\n"
		"  size: 1048576\n"
		"  flags: 2\n"
		"  mode: 0644\n"
		"  uid: 42\n"
		"  gid: 42\n"
		"  nextents: 3\n"
		"  simple_ext_list:\n"
		"  - offset: 0x38600000\n"
		"    length: 0x200000\n"
		"  - offset: 0x48600000\n"
		"    length: 0x200000\n"
		"  - offset: 0x58600000\n"
		"    length: 0x200000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, FAMFS_MAX_SIMPLE_EXTENTS,
				     FAMFS_MAX_SIMPLE_EXTENTS,2); /* 3 extents is not an overflow */
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(fm.fm_fmap.fmap_nextents, 3);
	ASSERT_EQ(fm.fm_fmap.se[0].se_offset, 0x38600000);
	ASSERT_EQ(fm.fm_fmap.se[0].se_len, 0x200000);
	ASSERT_EQ(fm.fm_fmap.se[1].se_offset, 0x48600000);
	ASSERT_EQ(fm.fm_fmap.se[1].se_len, 0x200000);
	ASSERT_EQ(fm.fm_fmap.se[2].se_offset, 0x58600000);
	ASSERT_EQ(fm.fm_fmap.se[2].se_len, 0x200000);

	/* Malformed extent list */
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 2,
				     FAMFS_MAX_SIMPLE_EXTENTS,2); /* 3 extents is an overflow this time */
	ASSERT_EQ(rc, -EOVERFLOW);
	my_yaml = "---\n" /* Good yaml */
		"file:\n"
		"  path: 0446\n"
		"  size: 1048576\n"
		"  flags: 2\n"
		"  mode: 0644\n"
		"  uid: 42\n"
		"  gid: 42\n"
		"  nextents: 1\n"
		"  simple_ext_list:\n"
		"  - length: 0x200000\n"   /* offset  and len should work any order */
		"    offset: 0x38600000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 1, FAMFS_MAX_SIMPLE_EXTENTS, 2);
	ASSERT_EQ(rc, 0);

	/* Length missing on one extent */
	my_yaml = "---\n" /* Good yaml */
		"file:\n"
		"  path: 0446\n"
		"  size: 1048576\n"
		"  flags: 2\n"
		"  mode: 0644\n"
		"  uid: 42\n"
		"  gid: 42\n"
		"  nextents: 3\n"
		"  simple_ext_list:\n"
		"  - offset: 0x38600000\n"
		"    length: 0x200000\n"
		"  - offset: 0x48600000\n"
		"  - offset: 0x58600000\n"
		"    length: 0x200000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, FAMFS_MAX_SIMPLE_EXTENTS, FAMFS_MAX_SIMPLE_EXTENTS, 2); /* 3 extents is not an overflow */
	ASSERT_EQ(rc, -EINVAL);

	/* offset followed by something other than length on ext list */
	my_yaml = "---\n" /* Good yaml */
		"file:\n"
		"  path: 0446\n"
		"  size: 1048576\n"
		"  flags: 2\n"
		"  mode: 0644\n"
		"  uid: 42\n"
		"  gid: 42\n"
		"  nextents: 3\n"
		"  simple_ext_list:\n"
		"  - offset: 0x38600000\n"
		"    length: 0x200000\n"
		"  - offset: 0x48600000\n"
		"    fubar: 0x200000\n"
		"  - offset: 0x58600000\n"
		"    length: 0x200000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, FAMFS_MAX_SIMPLE_EXTENTS, FAMFS_MAX_SIMPLE_EXTENTS, 2); /* 3 extents is not an overflow */
	ASSERT_EQ(rc, -EINVAL);

	printf("%s", yaml_event_str(YAML_NO_EVENT));
	printf("%s", yaml_event_str(YAML_ALIAS_EVENT));
	printf("%s", yaml_event_str(1000));


	/* Let's try parsing some striped extent yaml */

	my_yaml = "---\n" /* Good yaml */
		"file:\n"
		"  path: 0446\n"
		"  size: 1048576\n"
		"  flags: 2\n"
		"  mode: 0644\n"
		"  uid: 42\n"
		"  gid: 42\n"
		"  nextents: 1\n"
		"  striped_ext_list:\n"
		"  - offset: 0x38600000\n"
		"    length: 0x200000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 1, FAMFS_MAX_SIMPLE_EXTENTS, 2);
	ASSERT_EQ(rc, 0);

	my_yaml = "---\n"
		"file:\n"
		"  path: interleaved-file0\n"
		"  size: 3145728\n"
		"  flags: 2\n"
		"  mode: 00\n"
		"  uid: 0\n"
		"  gid: 0\n"
		"  nextents: 1\n"
		"  striped_ext_list:\n"
		"  - nstrips: 8\n"
		"    chunk_size: 0x200000\n"
		"    simple_ext_list:\n"
		"    - offset: 0x8600000\n"
		"      length: 0x200000\n"
		"    - offset: 0x2600000\n"
		"      length: 0x200000\n"
		"    - offset: 0xc600000\n"
		"      length: 0x200000\n"
		"    - offset: 0xe600000\n"
		"      length: 0x200000\n"
		"    - offset: 0x6600000\n"
		"      length: 0x200000\n"
		"    - offset: 0xa600000\n"
		"      length: 0x200000\n"
		"    - offset: 0x4600000\n"
		"      length: 0x200000\n"
		"    - offset: 0x1200000\n"
		"      length: 0x200000\n"
		"...";
	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 1, 3 /* max strips */, 2);
	ASSERT_NE(rc, 0);

	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 1, 8 /* max strips */, 2);
	ASSERT_EQ(rc, 0);

	famfs_yaml_test_reset(&fm, fp, my_yaml);
	rc = famfs_parse_shadow_yaml(fp, &fm, 1, 100 /* max strips */, 2);
	ASSERT_EQ(rc, 0);

}

static void famfs_yaml_stripe_reset(struct famfs_interleave_param *interleave_param, FILE *fp, char *yaml_str)
{
	memset(interleave_param, 0, sizeof(*interleave_param));
	rewind(fp);
	truncate_stream(fp, 0);
	fprintf(fp, "%s", yaml_str);
	rewind(fp);
}


TEST(famfs, famfs_config_yaml) {
	struct famfs_interleave_param interleave_param;
	u64 devsize = 8 * 1024ULL * 1024ULL * 1024ULL;
	FILE *fp = tmpfile();
	char *my_yaml;
	int rc;

	my_yaml = "---\n" /* Good yaml */
		"interleaved_alloc:\n"
		"  nbuckets: 8\n"
		"  nstrips: 6\n"
		"  chunk_size: 2m\n"
		"...";
	famfs_yaml_stripe_reset(&interleave_param, fp, my_yaml);

	rc = famfs_parse_alloc_yaml(fp, &interleave_param, 1);
	ASSERT_EQ(rc, 0);

	rc = famfs_validate_interleave_param(&interleave_param, 0x200000, devsize, 1);
	ASSERT_EQ(rc, 0);

	/* Different order */
	my_yaml = "---\n" /* Good yaml */
		"interleaved_alloc:\n"
		"  chunk_size: 2m\n"
		"  nstrips: 6\n"
		"  nbuckets: 8\n"
		"...";
	famfs_yaml_stripe_reset(&interleave_param, fp, my_yaml);

	rc = famfs_parse_alloc_yaml(fp, &interleave_param, 1);
	ASSERT_EQ(rc, 0);

	rc = famfs_validate_interleave_param(&interleave_param, 0x200000, devsize, 1);
	ASSERT_EQ(rc, 0);

	/* Bad chunk_size */
	my_yaml = "---\n" /* Good yaml */
		"interleaved_alloc:\n"
		"  chunk_size: 2\n"
		"  nstrips: 6\n"
		"  nbuckets: 8\n"
		"...";

	famfs_yaml_stripe_reset(&interleave_param, fp, my_yaml);
	rc = famfs_parse_alloc_yaml(fp, &interleave_param, 1);
	ASSERT_EQ(rc, 0);

	rc = famfs_validate_interleave_param(&interleave_param, 0x200000, devsize, 1);
	ASSERT_NE(rc, 0);

	/* Another bad chunk_size */
	my_yaml = "---\n" /* Good yaml */
		"interleaved_alloc:\n"
		"  chunk_size: 3000000\n"
		"  nstrips: 6\n"
		"  nbuckets: 8\n"
		"...";

	famfs_yaml_stripe_reset(&interleave_param, fp, my_yaml);
	rc = famfs_parse_alloc_yaml(fp, &interleave_param, 1);
	ASSERT_EQ(rc, 0);

	rc = famfs_validate_interleave_param(&interleave_param, 0x200000, devsize, 1);
	ASSERT_NE(rc, 0);

	/* Null stripe is valid */
	famfs_yaml_stripe_reset(&interleave_param, fp, my_yaml);
	rc = famfs_validate_interleave_param(&interleave_param, 0x200000, devsize, 1);
	ASSERT_EQ(rc, 0);

	/* But null yaml is not */
	my_yaml = "---\n" /* Good yaml */
		"...";

	famfs_yaml_stripe_reset(&interleave_param, fp, my_yaml);
	rc = famfs_parse_alloc_yaml(fp, &interleave_param, 1);
	ASSERT_NE(rc, 0);
}

TEST(famfs, famfs_fmap_alloc_verify) {
	struct fmap_mem_header *fm;
	int rc;

	fm = alloc_simple_fmap(0);
	ASSERT_EQ((u64)fm, NULL);

	fm = alloc_simple_fmap(10);
	ASSERT_NE((u64)fm, NULL);
	rc = validate_mem_fmap(fm, 1, 1);
	ASSERT_EQ(rc, 0);
	free_mem_fmap(fm);

	fm = alloc_simple_fmap(16);
	ASSERT_NE((u64)fm, NULL);
	rc = validate_mem_fmap(fm, 1, 1);
	ASSERT_EQ(rc, 0);
	free_mem_fmap(fm);

	fm = alloc_simple_fmap(17);
	ASSERT_EQ((u64)fm, NULL);
	rc = validate_mem_fmap(fm, 1, 1);
	ASSERT_EQ(rc, -1);
	free_mem_fmap(fm);

	fm = alloc_interleaved_fmap(1, 0, 1);
	ASSERT_EQ((u64)fm, NULL);
	rc = validate_mem_fmap(fm, 1, 1);
	ASSERT_EQ(rc, -1);
	free_mem_fmap(fm);

	fm = alloc_interleaved_fmap(1, 16, 1);
	ASSERT_NE((u64)fm, NULL);
	rc = validate_mem_fmap(fm, 1, 1);
	ASSERT_EQ(rc, 0);
	free_mem_fmap(fm);

	fm = alloc_interleaved_fmap(16, 16, 1);
	ASSERT_NE((u64)fm, NULL);
	rc = validate_mem_fmap(fm, 1, 1);
	ASSERT_EQ(rc, 0);
	free_mem_fmap(fm);

	fm = alloc_interleaved_fmap(17, 16, 1);
	ASSERT_EQ((u64)fm, NULL);
	rc = validate_mem_fmap(fm, 1, 1);
	ASSERT_EQ(rc, -1);
	free_mem_fmap(fm);
}

TEST(famfs, famfs_icache_test) {
	struct famfs_inode *inode, *prev_inode = NULL;
	char *shadow_root = "/tmp/test/root";
	struct bucket_series *bs = NULL;
	struct famfs_inode *root_inode;
	struct stat st;
	famfs_icache icache;
	s64 inode_num;
	u64 num_in_icache = 0;
#define NBUCKETS 10000

	memset(&st, 0, sizeof(st));
	memset(&icache, 0, sizeof(icache));
	
	system("mkdir /tmp/test/root");
	famfs_icache_init(NULL, &icache, shadow_root);
	ASSERT_EQ(icache.root.next, icache.root.prev);
	ASSERT_EQ(icache.count, 0);

	bucket_series_alloc(&bs, NBUCKETS, 2); /* inode #1 is reserved for root */

	/* Get the root inode */
	root_inode = famfs_icache_find_get_from_ino_locked(&icache, 1);
	ASSERT_EQ(root_inode->ino, 1);
	ASSERT_EQ(root_inode->flags, 1);
	ASSERT_EQ(root_inode->ftype, FAMFS_FDIR);
	prev_inode = root_inode;

	/* Depth: Each next inode is child of previous */
	while ((inode_num = bucket_series_next(bs)) != -1) {
		inode = famfs_inode_alloc(&icache,
					  -1 /* fd */,
					  "bogusname",
					  inode_num,
					  0           /* dev */,
					  NULL        /* fmeta */,
					  &st         /* attr / stat */,
					  FAMFS_FDIR  /* ftype */,
					  prev_inode  /* parent */);
		ASSERT_EQ(inode->ino, inode_num);
		num_in_icache++;
		famfs_icache_insert_locked(&icache, inode);
		ASSERT_EQ(num_in_icache, famfs_icache_count(&icache));

		if (num_in_icache > 1) {
			/* refcount of root_inode should be 3 or 4... */
			ASSERT_EQ(prev_inode->refcount, 2);
		}

		/* Put the holder ref on the inode we inserted */
		famfs_inode_putref_locked(inode, 1);
		prev_inode = inode;
	}
	ASSERT_EQ(icache.count, NBUCKETS);

	dump_icache(&icache, FAMFS_LOG_NOTICE);

	ASSERT_EQ(bs->current, NBUCKETS);
	bucket_series_rewind(bs);

	/* Delete all nodes from the cache in insert order */
	u64 loopct = 0;
	while ((inode_num = bucket_series_next(bs)) != -1) {
		struct famfs_inode *inode =
			famfs_icache_find_get_from_ino(&icache, inode_num);

		ASSERT_NE(inode, (struct famfs_inode *)NULL);
		ASSERT_EQ(inode_num, inode->ino);

		num_in_icache--;
		loopct++;

		/* Put one ref for the find above, and one to "free" the inode */
		famfs_inode_putref_locked(inode, 2);

		/* Cache should not shrink because all but last have refs */
		if (num_in_icache > 0) {
			ASSERT_EQ(num_in_icache + loopct, NBUCKETS);
		}
	}

	bucket_series_rewind(bs);

	/* Breadth: each new inode is child of root */
	while ((inode_num = bucket_series_next(bs)) != -1) {
		char name[PATH_MAX];

		snprintf(name, PATH_MAX - 1, "file%lld", inode_num);
		inode = famfs_inode_alloc(&icache,
					  -1         /* fd */,
					  name,
					  inode_num,
					  0          /* dev */,
					  NULL       /* fmap */,
					  &st        /* attr / stat */,
					  FAMFS_FDIR /* file type */,
					  root_inode /* parent */);
		ASSERT_EQ(inode->ino, inode_num);
		num_in_icache++;
		famfs_icache_insert_locked(&icache, inode);
		ASSERT_EQ(num_in_icache, icache.count);

		ASSERT_EQ(root_inode->refcount, 3 + num_in_icache);
	}
	ASSERT_EQ(icache.count, NBUCKETS);

	ASSERT_EQ(bs->current, NBUCKETS);
	bucket_series_rewind(bs);

	/* Delete all nodes from the cache in insert order */
	loopct = 0;
	while ((inode_num = bucket_series_next(bs)) != -1) {
		struct famfs_inode *inode =
			famfs_icache_find_get_from_ino_locked(&icache,
							      inode_num);

		ASSERT_NE(inode, (struct famfs_inode *)NULL);
		ASSERT_EQ(inode_num, inode->ino);

		num_in_icache--;
		loopct++;

		/* Put one ref for the find above, and one to "free" the inode */
		famfs_inode_putref_locked(inode, 2);

		/* Cache should not shrink because all but last have refs */
		if (num_in_icache > 0) {
			ASSERT_EQ(num_in_icache + loopct, NBUCKETS);
		}
		/* Put the holder ref on the inode we inserted */
		famfs_inode_putref_locked(inode, 1);
	}
	
	/* Put the root inode to go back to refcount=2 */
	famfs_inode_putref(root_inode);
	bucket_series_rewind(bs);

	/* Depth again, to be cleaned up by famfs_icache_destroy() */
	while ((inode_num = bucket_series_next(bs)) != -1) {
		inode = famfs_inode_alloc(&icache,
					  -1 /* fd */,
					  "bogusname",
					  inode_num,
					  0           /* dev */,
					  NULL        /* fmeta */,
					  &st         /* attr / stat */,
					  FAMFS_FDIR  /* ftype */,
					  prev_inode  /* parent */);
		ASSERT_EQ(inode->ino, inode_num);
		num_in_icache++;
		famfs_icache_insert_locked(&icache, inode);
		ASSERT_EQ(num_in_icache, icache.count);

		if (num_in_icache > 1) {
			/* refcount of root_inode should be 3 or 4... */
			ASSERT_EQ(prev_inode->refcount, 2);
		}

		/* Put the holder ref on the inode we inserted */
		famfs_inode_putref_locked(inode, 1);
		prev_inode = inode;
	}
	ASSERT_EQ(icache.count, NBUCKETS);

	ASSERT_EQ(bs->current, NBUCKETS);
	bucket_series_rewind(bs);

	bucket_series_destroy(bs);

	famfs_icache_destroy(&icache);
}

TEST(famfs, famfs_log_test) {
	famfs_log(FAMFS_LOG_NOTICE, "%s:\n", __func__);
	famfs_log(FAMFS_INVALID, "bad log level\n");

	printf("0: %s\n", famfs_log_level_string(0));
	printf("1: %s\n", famfs_log_level_string(1));
	printf("2: %s\n", famfs_log_level_string(2));
	printf("3: %s\n", famfs_log_level_string(3));
	printf("4: %s\n", famfs_log_level_string(4));
	printf("5: %s\n", famfs_log_level_string(5));
	printf("6: %s\n", famfs_log_level_string(6));
	printf("7: %s\n", famfs_log_level_string(7));
	printf("8: %s\n", famfs_log_level_string(8)); /* invalid */

	ASSERT_EQ(famfs_log_get_level(), FAMFS_LOG_NOTICE);

	famfs_log_set_level(FAMFS_LOG_DEBUG);
	ASSERT_EQ(famfs_log_get_level(), FAMFS_LOG_DEBUG);
	famfs_log_set_level(FAMFS_INVALID); /* Invalid level - level should not change */
	ASSERT_EQ(famfs_log_get_level(), FAMFS_LOG_DEBUG);

	famfs_log_disable_syslog();
	famfs_log(FAMFS_LOG_NOTICE, "%s:\n", __func__);
}

TEST(famfs, famfs_daxdev) {
	int rc;
	rc = famfs_bounce_daxdev("bogusdev", 2);
	printf("famfs_bounce_daxdev = %d\n", rc);
	ASSERT_NE(rc, 0);
}
