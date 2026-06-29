// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 *
 * Unit tests for famfs_config - git-style hierarchical configuration system
 */

#include <gtest/gtest.h>

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <errno.h>

#include "famfs_config.h"
#include "famfs_lib.h"
}

/* Test fixture for config tests */
class FamfsConfigTest : public ::testing::Test {
protected:
	char test_dir[PATH_MAX];
	char user_config_dir[PATH_MAX];
	char user_config_file[PATH_MAX];
	char local_config_dir[PATH_MAX];
	char local_config_file[PATH_MAX];
	char *original_home;

	void SetUp() override {
		/* Create temporary test directories */
		snprintf(test_dir, sizeof(test_dir), "/tmp/famfs_config_test_%d", getpid());
		mkdir(test_dir, 0755);

		/* Setup fake user config directory */
		snprintf(user_config_dir, sizeof(user_config_dir), "%s/.famfs", test_dir);
		mkdir(user_config_dir, 0755);
		snprintf(user_config_file, sizeof(user_config_file), "%s/famfs.conf", user_config_dir);

		/* Setup fake local config directory (shadow path) */
		snprintf(local_config_dir, sizeof(local_config_dir), "%s/shadow", test_dir);
		mkdir(local_config_dir, 0755);
		snprintf(local_config_file, sizeof(local_config_file), "%s/config", local_config_dir);

		/* Override HOME for user config tests */
		original_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
		setenv("HOME", test_dir, 1);
	}

	void TearDown() override {
		/* Restore original HOME */
		if (original_home) {
			setenv("HOME", original_home, 1);
			free(original_home);
		} else {
			unsetenv("HOME");
		}

		/* Cleanup test files */
		unlink(user_config_file);
		unlink(local_config_file);
		rmdir(user_config_dir);
		rmdir(local_config_dir);
		rmdir(test_dir);
	}

	/* Helper to write a config file */
	void write_config_file(const char *path, const char *content) {
		FILE *fp = fopen(path, "w");
		ASSERT_NE(fp, nullptr) << "Failed to create config file: " << path;
		fprintf(fp, "%s", content);
		fclose(fp);
	}
};

/*
 * Test famfs_config_init
 */
TEST_F(FamfsConfigTest, config_init_zeros_structure)
{
	struct famfs_config cfg;

	/* Fill with garbage first */
	memset(&cfg, 0xFF, sizeof(cfg));

	famfs_config_init(&cfg);

	/* Verify all fields are zeroed */
	EXPECT_EQ(cfg.nbuckets, 0u);
	EXPECT_FALSE(cfg.nbuckets_set);
	EXPECT_EQ(cfg.nstrips, 0u);
	EXPECT_FALSE(cfg.nstrips_set);
	EXPECT_EQ(cfg.chunk_size, 0u);
	EXPECT_FALSE(cfg.chunk_size_set);
	EXPECT_EQ(cfg.verbose, 0);
	EXPECT_FALSE(cfg.verbose_set);
	EXPECT_EQ(cfg.threadct, 0);
	EXPECT_FALSE(cfg.threadct_set);
}

/*
 * Test famfs_config_scope_name
 */
TEST_F(FamfsConfigTest, scope_name_returns_correct_strings)
{
	EXPECT_STREQ(famfs_config_scope_name(FAMFS_CONFIG_SCOPE_SYSTEM), "system");
	EXPECT_STREQ(famfs_config_scope_name(FAMFS_CONFIG_SCOPE_USER), "user");
	EXPECT_STREQ(famfs_config_scope_name(FAMFS_CONFIG_SCOPE_LOCAL), "local");
	EXPECT_STREQ(famfs_config_scope_name((enum famfs_config_scope)99), "unknown");
}

/*
 * Test famfs_config_get_path
 */
TEST_F(FamfsConfigTest, get_path_system_scope)
{
	char path[PATH_MAX];
	int rc;

	rc = famfs_config_get_path(FAMFS_CONFIG_SCOPE_SYSTEM, NULL, path, sizeof(path));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(path, "/etc/famfs.conf");
}

TEST_F(FamfsConfigTest, get_path_user_scope)
{
	char path[PATH_MAX];
	char expected[PATH_MAX];
	int rc;

	snprintf(expected, sizeof(expected), "%s/.famfs/famfs.conf", test_dir);

	rc = famfs_config_get_path(FAMFS_CONFIG_SCOPE_USER, NULL, path, sizeof(path));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(path, expected);
}

TEST_F(FamfsConfigTest, get_path_local_scope)
{
	char path[PATH_MAX];
	char expected[PATH_MAX];
	int rc;

	snprintf(expected, sizeof(expected), "%s/config", local_config_dir);

	rc = famfs_config_get_path(FAMFS_CONFIG_SCOPE_LOCAL, local_config_dir, path, sizeof(path));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(path, expected);
}

TEST_F(FamfsConfigTest, get_path_local_scope_requires_shadow_path)
{
	char path[PATH_MAX];
	int rc;

	rc = famfs_config_get_path(FAMFS_CONFIG_SCOPE_LOCAL, NULL, path, sizeof(path));
	EXPECT_EQ(rc, -EINVAL);
}

TEST_F(FamfsConfigTest, get_path_invalid_scope)
{
	char path[PATH_MAX];
	int rc;

	rc = famfs_config_get_path((enum famfs_config_scope)99, NULL, path, sizeof(path));
	EXPECT_EQ(rc, -EINVAL);
}

/*
 * Test famfs_config_load_file
 */
TEST_F(FamfsConfigTest, load_file_nonexistent_returns_1)
{
	struct famfs_config cfg;
	int rc;

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, "/nonexistent/path/config.yaml", 0);
	EXPECT_EQ(rc, 1);
}

TEST_F(FamfsConfigTest, load_file_parses_interleave_section)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 8\n"
		"  nstrips: 6\n"
		"  chunk_size: 2097152\n";

	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(cfg.nbuckets_set);
	EXPECT_EQ(cfg.nbuckets, 8u);
	EXPECT_TRUE(cfg.nstrips_set);
	EXPECT_EQ(cfg.nstrips, 6u);
	EXPECT_TRUE(cfg.chunk_size_set);
	EXPECT_EQ(cfg.chunk_size, 2097152u);
}

TEST_F(FamfsConfigTest, load_file_parses_chunk_size_with_suffix)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"interleave:\n"
		"  chunk_size: 2M\n";

	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(cfg.chunk_size_set);
	EXPECT_EQ(cfg.chunk_size, 2u * 1024 * 1024);
}

TEST_F(FamfsConfigTest, load_file_parses_core_section)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"core:\n"
		"  verbose: 2\n"
		"  threadct: 8\n";

	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(cfg.verbose_set);
	EXPECT_EQ(cfg.verbose, 2);
	EXPECT_TRUE(cfg.threadct_set);
	EXPECT_EQ(cfg.threadct, 8);
}

TEST_F(FamfsConfigTest, load_file_parses_both_sections)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 4\n"
		"  nstrips: 3\n"
		"core:\n"
		"  verbose: 1\n"
		"  threadct: 4\n";

	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.nbuckets, 4u);
	EXPECT_EQ(cfg.nstrips, 3u);
	EXPECT_EQ(cfg.verbose, 1);
	EXPECT_EQ(cfg.threadct, 4);
}

TEST_F(FamfsConfigTest, load_file_ignores_unknown_sections)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"unknown_section:\n"
		"  foo: bar\n"
		"interleave:\n"
		"  nbuckets: 8\n";

	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(cfg.nbuckets_set);
	EXPECT_EQ(cfg.nbuckets, 8u);
}

TEST_F(FamfsConfigTest, load_file_ignores_unknown_keys)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 8\n"
		"  unknown_key: value\n"
		"  nstrips: 4\n";

	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.nbuckets, 8u);
	EXPECT_EQ(cfg.nstrips, 4u);
}

/*
 * Test famfs_config_load (hierarchical loading)
 */
TEST_F(FamfsConfigTest, load_merges_user_and_local)
{
	struct famfs_config cfg;
	int rc;

	/* User config sets nbuckets=4, nstrips=2 */
	const char *user_content =
		"interleave:\n"
		"  nbuckets: 4\n"
		"  nstrips: 2\n";
	write_config_file(user_config_file, user_content);

	/* Local config overrides nstrips=8 */
	const char *local_content =
		"interleave:\n"
		"  nstrips: 8\n";
	write_config_file(local_config_file, local_content);

	rc = famfs_config_load(&cfg, local_config_dir, 0);

	EXPECT_EQ(rc, 0);
	/* nbuckets from user config */
	EXPECT_EQ(cfg.nbuckets, 4u);
	/* nstrips overridden by local config */
	EXPECT_EQ(cfg.nstrips, 8u);
}

TEST_F(FamfsConfigTest, load_without_shadow_path_skips_local)
{
	struct famfs_config cfg;
	int rc;

	const char *user_content =
		"interleave:\n"
		"  nbuckets: 10\n";
	write_config_file(user_config_file, user_content);

	const char *local_content =
		"interleave:\n"
		"  nbuckets: 99\n";
	write_config_file(local_config_file, local_content);

	rc = famfs_config_load(&cfg, NULL, 0);

	EXPECT_EQ(rc, 0);
	/* Should get user config value, not local */
	EXPECT_EQ(cfg.nbuckets, 10u);
}

/*
 * Test famfs_config_apply_interleave
 */
TEST_F(FamfsConfigTest, apply_interleave_only_sets_configured_values)
{
	struct famfs_config cfg;
	struct famfs_interleave_param ip = {0};

	/* Set initial values */
	ip.nbuckets = 100;
	ip.nstrips = 100;
	ip.chunk_size = 100;

	/* Config only sets nbuckets */
	famfs_config_init(&cfg);
	cfg.nbuckets = 8;
	cfg.nbuckets_set = true;

	famfs_config_apply_interleave(&cfg, &ip);

	EXPECT_EQ(ip.nbuckets, 8u);
	EXPECT_EQ(ip.nstrips, 100u);    /* Unchanged */
	EXPECT_EQ(ip.chunk_size, 100u); /* Unchanged */
}

TEST_F(FamfsConfigTest, apply_interleave_sets_all_configured_values)
{
	struct famfs_config cfg;
	struct famfs_interleave_param ip = {0};

	famfs_config_init(&cfg);
	cfg.nbuckets = 8;
	cfg.nbuckets_set = true;
	cfg.nstrips = 6;
	cfg.nstrips_set = true;
	cfg.chunk_size = 2 * 1024 * 1024;
	cfg.chunk_size_set = true;

	famfs_config_apply_interleave(&cfg, &ip);

	EXPECT_EQ(ip.nbuckets, 8u);
	EXPECT_EQ(ip.nstrips, 6u);
	EXPECT_EQ(ip.chunk_size, 2u * 1024 * 1024);
}

/*
 * Test famfs_config_set
 */
TEST_F(FamfsConfigTest, set_creates_new_config_file)
{
	int rc;
	struct famfs_config cfg;

	/* Ensure file doesn't exist */
	unlink(user_config_file);

	rc = famfs_config_set("interleave.nbuckets", "16",
			      FAMFS_CONFIG_SCOPE_USER, NULL);
	EXPECT_EQ(rc, 0);

	/* Verify the file was created and contains the value */
	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(cfg.nbuckets_set);
	EXPECT_EQ(cfg.nbuckets, 16u);
}

TEST_F(FamfsConfigTest, set_updates_existing_key)
{
	int rc;
	struct famfs_config cfg;

	const char *initial_content =
		"interleave:\n"
		"  nbuckets: 4\n"
		"  nstrips: 2\n";
	write_config_file(user_config_file, initial_content);

	rc = famfs_config_set("interleave.nbuckets", "32",
			      FAMFS_CONFIG_SCOPE_USER, NULL);
	EXPECT_EQ(rc, 0);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.nbuckets, 32u);
	EXPECT_EQ(cfg.nstrips, 2u); /* Unchanged */
}

TEST_F(FamfsConfigTest, set_adds_new_key_to_existing_section)
{
	int rc;
	struct famfs_config cfg;

	const char *initial_content =
		"interleave:\n"
		"  nbuckets: 4\n";
	write_config_file(user_config_file, initial_content);

	rc = famfs_config_set("interleave.nstrips", "8",
			      FAMFS_CONFIG_SCOPE_USER, NULL);
	EXPECT_EQ(rc, 0);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.nbuckets, 4u);
	EXPECT_EQ(cfg.nstrips, 8u);
}

TEST_F(FamfsConfigTest, set_creates_new_section)
{
	int rc;
	struct famfs_config cfg;

	const char *initial_content =
		"interleave:\n"
		"  nbuckets: 4\n";
	write_config_file(user_config_file, initial_content);

	rc = famfs_config_set("core.verbose", "2",
			      FAMFS_CONFIG_SCOPE_USER, NULL);
	EXPECT_EQ(rc, 0);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.nbuckets, 4u);
	EXPECT_EQ(cfg.verbose, 2);
}

TEST_F(FamfsConfigTest, set_rejects_invalid_key_format)
{
	int rc;

	rc = famfs_config_set("invalid_key_no_dot", "value",
			      FAMFS_CONFIG_SCOPE_USER, NULL);
	EXPECT_EQ(rc, -EINVAL);
}

TEST_F(FamfsConfigTest, set_local_scope)
{
	int rc;
	struct famfs_config cfg;

	rc = famfs_config_set("interleave.nbuckets", "24",
			      FAMFS_CONFIG_SCOPE_LOCAL, local_config_dir);
	EXPECT_EQ(rc, 0);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, local_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.nbuckets, 24u);
}

TEST_F(FamfsConfigTest, set_local_scope_requires_shadow_path)
{
	int rc;

	rc = famfs_config_set("interleave.nbuckets", "24",
			      FAMFS_CONFIG_SCOPE_LOCAL, NULL);
	EXPECT_EQ(rc, -EINVAL);
}

/*
 * Test famfs_config_get
 */
TEST_F(FamfsConfigTest, get_returns_configured_value)
{
	int rc;
	char value[64];

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 12\n"
		"  nstrips: 6\n"
		"  chunk_size: 4194304\n"
		"core:\n"
		"  verbose: 3\n"
		"  threadct: 16\n";
	write_config_file(user_config_file, config_content);

	rc = famfs_config_get("interleave.nbuckets", -1, NULL, value, sizeof(value));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(value, "12");

	rc = famfs_config_get("interleave.nstrips", -1, NULL, value, sizeof(value));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(value, "6");

	rc = famfs_config_get("interleave.chunk_size", -1, NULL, value, sizeof(value));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(value, "4194304");

	rc = famfs_config_get("core.verbose", -1, NULL, value, sizeof(value));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(value, "3");

	rc = famfs_config_get("core.threadct", -1, NULL, value, sizeof(value));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(value, "16");
}

TEST_F(FamfsConfigTest, get_returns_1_when_not_set)
{
	int rc;
	char value[64];

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 8\n";
	write_config_file(user_config_file, config_content);

	rc = famfs_config_get("interleave.nstrips", -1, NULL, value, sizeof(value));
	EXPECT_EQ(rc, 1);
}

TEST_F(FamfsConfigTest, get_returns_1_for_unknown_key)
{
	int rc;
	char value[64];

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 8\n";
	write_config_file(user_config_file, config_content);

	rc = famfs_config_get("unknown.key", -1, NULL, value, sizeof(value));
	EXPECT_EQ(rc, 1);
}

TEST_F(FamfsConfigTest, get_from_specific_scope)
{
	int rc;
	char value[64];

	const char *user_content =
		"interleave:\n"
		"  nbuckets: 4\n";
	write_config_file(user_config_file, user_content);

	const char *local_content =
		"interleave:\n"
		"  nbuckets: 16\n";
	write_config_file(local_config_file, local_content);

	/* Get from user scope only */
	rc = famfs_config_get("interleave.nbuckets", FAMFS_CONFIG_SCOPE_USER,
			      NULL, value, sizeof(value));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(value, "4");

	/* Get from local scope only */
	rc = famfs_config_get("interleave.nbuckets", FAMFS_CONFIG_SCOPE_LOCAL,
			      local_config_dir, value, sizeof(value));
	EXPECT_EQ(rc, 0);
	EXPECT_STREQ(value, "16");
}

/*
 * Test famfs_config_list
 */
TEST_F(FamfsConfigTest, list_outputs_all_configured_values)
{
	int rc;
	char output_file[PATH_MAX];
	FILE *fp;
	char buffer[1024];

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 8\n"
		"  nstrips: 4\n"
		"core:\n"
		"  verbose: 1\n";
	write_config_file(user_config_file, config_content);

	snprintf(output_file, sizeof(output_file), "%s/list_output.txt", test_dir);
	fp = fopen(output_file, "w");
	ASSERT_NE(fp, nullptr);

	rc = famfs_config_list(FAMFS_CONFIG_SCOPE_USER, NULL, false, fp);
	EXPECT_EQ(rc, 0);
	fclose(fp);

	/* Read and verify output */
	fp = fopen(output_file, "r");
	ASSERT_NE(fp, nullptr);
	memset(buffer, 0, sizeof(buffer));
	fread(buffer, 1, sizeof(buffer) - 1, fp);
	fclose(fp);

	EXPECT_NE(strstr(buffer, "interleave.nbuckets=8"), nullptr);
	EXPECT_NE(strstr(buffer, "interleave.nstrips=4"), nullptr);
	EXPECT_NE(strstr(buffer, "core.verbose=1"), nullptr);

	unlink(output_file);
}

TEST_F(FamfsConfigTest, list_with_show_origin_includes_file_path)
{
	int rc;
	char output_file[PATH_MAX];
	FILE *fp;
	char buffer[1024];

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 8\n";
	write_config_file(user_config_file, config_content);

	snprintf(output_file, sizeof(output_file), "%s/list_output.txt", test_dir);
	fp = fopen(output_file, "w");
	ASSERT_NE(fp, nullptr);

	rc = famfs_config_list(FAMFS_CONFIG_SCOPE_USER, NULL, true, fp);
	EXPECT_EQ(rc, 0);
	fclose(fp);

	fp = fopen(output_file, "r");
	ASSERT_NE(fp, nullptr);
	memset(buffer, 0, sizeof(buffer));
	fread(buffer, 1, sizeof(buffer) - 1, fp);
	fclose(fp);

	/* Should contain file path comment */
	EXPECT_NE(strstr(buffer, "# "), nullptr);
	EXPECT_NE(strstr(buffer, "(user)"), nullptr);

	unlink(output_file);
}

TEST_F(FamfsConfigTest, list_empty_file_produces_no_output)
{
	int rc;
	char output_file[PATH_MAX];
	FILE *fp;
	char buffer[64];
	size_t bytes_read;

	/* Don't create any config file */
	unlink(user_config_file);

	snprintf(output_file, sizeof(output_file), "%s/list_output.txt", test_dir);
	fp = fopen(output_file, "w");
	ASSERT_NE(fp, nullptr);

	rc = famfs_config_list(FAMFS_CONFIG_SCOPE_USER, NULL, false, fp);
	EXPECT_EQ(rc, 0);
	fclose(fp);

	fp = fopen(output_file, "r");
	ASSERT_NE(fp, nullptr);
	bytes_read = fread(buffer, 1, sizeof(buffer), fp);
	fclose(fp);

	EXPECT_EQ(bytes_read, 0u);

	unlink(output_file);
}

/*
 * Test edge cases
 */
TEST_F(FamfsConfigTest, handles_empty_config_file)
{
	struct famfs_config cfg;
	int rc;

	write_config_file(user_config_file, "");

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_FALSE(cfg.nbuckets_set);
	EXPECT_FALSE(cfg.nstrips_set);
}

TEST_F(FamfsConfigTest, handles_zero_values)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"interleave:\n"
		"  nbuckets: 0\n"
		"core:\n"
		"  verbose: 0\n";
	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(cfg.nbuckets_set);
	EXPECT_EQ(cfg.nbuckets, 0u);
	EXPECT_TRUE(cfg.verbose_set);
	EXPECT_EQ(cfg.verbose, 0);
}

TEST_F(FamfsConfigTest, handles_large_values)
{
	struct famfs_config cfg;
	int rc;

	const char *config_content =
		"interleave:\n"
		"  chunk_size: 1099511627776\n";  /* 1TB */
	write_config_file(user_config_file, config_content);

	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(cfg.chunk_size_set);
	EXPECT_EQ(cfg.chunk_size, 1099511627776ULL);
}

TEST_F(FamfsConfigTest, handles_chunk_size_suffixes)
{
	struct famfs_config cfg;
	int rc;

	/* Test K suffix */
	write_config_file(user_config_file, "interleave:\n  chunk_size: 64K\n");
	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.chunk_size, 64u * 1024);

	/* Test M suffix */
	write_config_file(user_config_file, "interleave:\n  chunk_size: 4M\n");
	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.chunk_size, 4u * 1024 * 1024);

	/* Test G suffix */
	write_config_file(user_config_file, "interleave:\n  chunk_size: 1G\n");
	famfs_config_init(&cfg);
	rc = famfs_config_load_file(&cfg, user_config_file, 0);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(cfg.chunk_size, 1ULL * 1024 * 1024 * 1024);
}
