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
	char *dev_out)
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
		char *xpath = NULL;

		if (strstr(line, "famfs")) {
			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, args, &x0, &x1);
			if (rc <= 0)
				goto out;

			xmpt = realpath(mpt, NULL);
			if (!xmpt) {				fprintf(stderr, "realpath(%s) errno %d\n", mpt, errno);
				continue;
			}
			xpath = realpath(path, NULL);
			if (!xpath) {
				fprintf(stderr, "input path realpath(%s) errno %d\n", path, errno);
				free(xmpt);
				continue;
			}
			if (strcmp(xpath, xmpt) == 0) {
				free(xmpt);
				free(xpath);
				free(line);
				fclose(fp);
				if (dev_out)
					strcpy(dev_out, dev);
				return 1;
			}
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
