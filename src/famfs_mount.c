// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <linux/types.h>
#include <stddef.h>
#include <linux/ioctl.h>
#include <libgen.h>
#include <assert.h>
#include <sys/param.h> /* MIN()/MAX() */
#include <sys/file.h>
#include <sys/statfs.h>

#include "famfs_lib_internal.h"

/* XXX TODO:
 * three funcs in this file share code that's very similar:
 * * famfs_get_mpt_by_dev
 * * famfs_path_is_mount_pt
 * * xx
 *
 * Can this be re-factored with less duplication?
 */

#define XLEN 256

/**
 * famfs_get_mpt_by_dev()
 *
 * @mtdev: the primary dax device for a famfs file system.
 *
 * This function determines the mount point by parsing /proc/mounts to find the mount point
 * from a dax device name.
 */
char *
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

static int
shadow_path_from_opts(const char *opts, char *shadow_path_out, size_t shadow_path_size)
{
	const char *start = opts;
	const char *end = opts;
	const char *keyword = "shadow=";
	size_t keyword_len = strlen(keyword);

	if (opts == NULL || shadow_path_out == NULL || shadow_path_size == 0) {
		return 0;  // Invalid opts
	}

	while (*end != '\0') {
		if (*end == ',' || *(end + 1) == '\0') {
			// Adjust end if it's the last character
			if (*(end + 1) == '\0') {
				end++;
			}

			// Check if the segment starts with "shadow="
			if ((end - start) >= keyword_len && strncmp(start, keyword, keyword_len) == 0) {
				const char *value_start = start + keyword_len;  // Skip "shadow="
				size_t value_length = end - value_start;

				if (value_length >= shadow_path_size) {
					return 0;  // Shadow_Path_Out buffer too small
				}

				strncpy(shadow_path_out, value_start, value_length);
				shadow_path_out[value_length] = '\0';
				return 1;  // Success
			}

			// Move to the next segment
			start = end + 1;
		}
		end++;
	}

	return 0;  // No matching argument found
}

/* XXX: this function should be renamed more descriptively */
/**
 * famfs_path_is_mount_pt()
 *
 * check whether a path is a famfs mount point via /proc/mounts
 *
 * @path:
 * @dev_out: if non-null, the device name will be copied here
 *
 * Return values
 * 1 - the path is an active famfs mount point
 * 0 - the path is not an active famfs mount point
 */
int
famfs_path_is_mount_pt(
	const char *path,
	char *dev_out,
	char *shadow_out)
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
		char opts[XLEN];
		char shadow_path[PATH_MAX];
		int  x0, x1;
		char *xmpt = NULL;
		char *xpath = NULL;

		if (strstr(line, "famfs")) {
			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, opts, &x0, &x1);
			if (rc <= 0)
				goto out;

			xmpt = realpath(mpt, NULL);
			if (!xmpt) {
				fprintf(stderr, "realpath(%s) errno %d\n", mpt, errno);
				continue;
			}
			xpath = realpath(path, NULL);
			if (!xpath) {
				fprintf(stderr, "input path realpath(%s) errno %d\n", path, errno);
				free(xmpt);
				continue;
			}
			if (strcmp(xpath, xmpt) != 0)
				continue;

			/* Path matches the mount point of this entry */

			if (shadow_out) {
				rc = shadow_path_from_opts(opts, shadow_path, sizeof(shadow_path));
				if (rc)
					strncpy(shadow_out, shadow_path, PATH_MAX - 1);
				else
					shadow_out[0] = 0;
			}

			free(xmpt);
			free(xpath);
			free(line);
			fclose(fp);
			if (dev_out)
				strcpy(dev_out, dev);
			return 1;
		}
		if (xmpt)
			free(xmpt);
		if (xpath)
		  free(xpath);

	}

out:
	fclose(fp);
	if (line)
		free(line);
	return 0;
}

/**
 * find_mount_point()
 *
 * @path - path to find mount point. @path need not be valid (it might be a
 *         file that is about to be created), but it's parent dir
 *         (i.e. dirname(path) must be valid - and a directory.
 */
char *find_mount_point(const char *path)
{
	struct statfs root_fs, fs, parent_fs;
	struct stat st, parent_st;
	char resolved_path[PATH_MAX];
	char *current_path = NULL;

	/* Get statfs for the root directory to get root fsid */
	if (statfs("/", &root_fs) != 0) {
		perror("statfs for root failed");
		return NULL;
	}

	/* Resolve initial path to absolute or parent if nonexistent */
	if (realpath(path, resolved_path) != NULL) {
		current_path = strdup(resolved_path);
	} else if (errno == ENOENT) {
		/* Path does not exist, check parent directory */
		char temp_path[PATH_MAX];
		strncpy(temp_path, path, PATH_MAX);
		while (realpath(dirname(temp_path), resolved_path) == NULL) {
			/* Continue ascending if parent also doesn't exist */
			strncpy(temp_path, dirname(temp_path), PATH_MAX);
		}
		current_path = strdup(resolved_path);
	} else {
		perror("realpath failed");
		return NULL;
	}

	/* Get initial statfs and stat info */
	if (statfs(current_path, &fs) != 0 || stat(current_path, &st) != 0) {
		perror("statfs or stat failed");
		free(current_path);
		return NULL;
	}

	/* Check if the current path is on the root file system */
	if (fs.f_fsid.__val[0] == root_fs.f_fsid.__val[0] &&
	    fs.f_fsid.__val[1] == root_fs.f_fsid.__val[1]) {
		free(current_path);
		return strdup("/");  // It's the root file system
	}

	/* Ascend the directory tree to find the mount point */
	while (1) {
		char parent_path[PATH_MAX];
		strcpy(parent_path, current_path);

		/* Move one level up in the directory tree */
		if (dirname(parent_path) == NULL) {
			break;
		}

		/* Get statfs and stat info for the parent directory */
		if (statfs(parent_path, &parent_fs) != 0
		    || stat(parent_path, &parent_st) != 0) {
			perror("statfs or stat failed");
			break;
		}

		/* Check if we have reached a different file system */
		if (parent_fs.f_fsid.__val[0] != fs.f_fsid.__val[0] ||
		    parent_fs.f_fsid.__val[1] != fs.f_fsid.__val[1] ||
		    parent_st.st_dev != st.st_dev) {
			break;  // We have found the mount point
		}

		/* Move up if still the same file system */
		free(current_path);
		current_path = realpath(parent_path, NULL);
		if (current_path == NULL) {
			perror("realpath failed");
			return NULL;
		}
	}

	return current_path;  // This is the mount point path
}
