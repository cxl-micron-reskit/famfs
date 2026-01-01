// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

/*
 * reenable_devdax.c - Disable a DAX device then re-enable it in devdax mode
 *
 * Requires libdaxctl from the ndctl project.
 * Compile:
 *   gcc -O2 -Wall -Wextra -o reenable_devdax reenable_devdax.c $(pkg-config --cflags --libs libdaxctl)
 *
 * Usage:
 *   sudo ./reenable_devdax daxX.Y
 *   sudo ./reenable_devdax /dev/daxX.Y
 *
 * Notes:
 * - You must run as root.
 * - Reconfiguring device modes can be destructive in other modes. Enabling
 *   "devdax" itself is non-destructive, but flipping to/from system-ram is.
 * - This program only ensures the device is disabled, then enabled in devdax mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>

#include <daxctl/libdaxctl.h>

#include "famfs_lib.h"

#ifdef STANDALONE
static const char *basename_dev(const char *arg)
{
	/* Accept "dax0.0" or "/dev/dax0.0"; return "dax0.0" */
	const char *p = strrchr(arg, '/');
	return p ? p + 1 : arg;
}
#endif

const char *daxdev_mode_string(enum famfs_daxdev_mode mode)
{
	switch (mode) {
	case DAXDEV_MODE_UNKNOWN:
		return "UNKNOWN";
	case DAXDEV_MODE_DEVICE_DAX:
		return "devdax";
	case DAXDEV_MODE_FAMFS:
		return "famfs";
	}
	return "INVALID";
}

/**
 * famfs_get_daxdev_mode() - Determine which driver is bound to a daxdev
 * @daxdev: Path to the dax device (e.g., "/dev/dax1.0" or "dax1.0")
 *
 * Returns the mode enum based on reading the driver symlink in sysfs.
 */
enum famfs_daxdev_mode famfs_get_daxdev_mode(const char *daxdev)
{
	char syspath[PATH_MAX];
	char linkbuf[PATH_MAX];
	char *devname_copy;
	char *devbasename;
	char *driver_name;
	ssize_t len;

	if (!daxdev)
		return DAXDEV_MODE_UNKNOWN;

	/* Get basename of device path (handles both "/dev/dax1.0" and "dax1.0") */
	devname_copy = strdup(daxdev);
	if (!devname_copy)
		return DAXDEV_MODE_UNKNOWN;

	devbasename = basename(devname_copy);

	/* Construct sysfs driver symlink path */
	snprintf(syspath, sizeof(syspath),
		 "/sys/bus/dax/devices/%s/driver", devbasename);

	/* Read the symlink */
	len = readlink(syspath, linkbuf, sizeof(linkbuf) - 1);
	free(devname_copy);

	if (len < 0)
		return DAXDEV_MODE_UNKNOWN;

	linkbuf[len] = '\0';

	/* Get basename of the link target (e.g., "device_dax" or "famfs") */
	driver_name = basename(linkbuf);

	if (strcmp(driver_name, "device_dax") == 0)
		return DAXDEV_MODE_DEVICE_DAX;
	else if (strcmp(driver_name, "fsdev_dax") == 0)
		return DAXDEV_MODE_FAMFS;

	return DAXDEV_MODE_UNKNOWN;
}

/**
 * famfs_set_daxdev_mode() - Change the driver bound to a daxdev
 * @daxdev: Path to the dax device (e.g., "/dev/dax1.0" or "dax1.0")
 * @mode:   Target mode (DAXDEV_MODE_DEVICE_DAX or DAXDEV_MODE_FAMFS)
 *
 * Retries up to 10 times with exponential backoff if EBUSY is returned.
 *
 * Returns 0 on success, negative errno on failure.
 */
int famfs_set_daxdev_mode(
	const char *daxdev,
	enum famfs_daxdev_mode mode,
	int verbose)
{
	enum famfs_daxdev_mode current_mode;
	char unbind_path[PATH_MAX];
	char bind_path[PATH_MAX];
	const char *unbind_drv;
	const char *bind_drv;
	char *devname_copy = NULL;
	char *devbasename;
	int max_retries = 10;
	int delay_ms = 100;
	FILE *fp;
	int rc = 0;
	int i;

	if (verbose)
		printf("%s: change %s to mode %s\n", __func__,
		       daxdev, daxdev_mode_string(mode));
	if (!daxdev)
		return -EINVAL;

	if (mode != DAXDEV_MODE_DEVICE_DAX && mode != DAXDEV_MODE_FAMFS) {
		fprintf(stderr, "%s: invalid mode %d\n", __func__, mode);
		return -EINVAL;
	}

	/* Check current mode */
	current_mode = famfs_get_daxdev_mode(daxdev);
	if (current_mode == mode) {
		if (verbose)
			printf("%s: %s is already in mode %s\n", __func__,
			       daxdev, daxdev_mode_string(mode));
		return 0; /* Already in requested mode */
	}

	if (current_mode == DAXDEV_MODE_UNKNOWN) {
		fprintf(stderr, "%s: Can't change mode from %s\n",
			__func__, daxdev_mode_string(DAXDEV_MODE_UNKNOWN));
		return -ENODEV;
	}

	/* Get basename of device path */
	devname_copy = strdup(daxdev);
	if (!devname_copy)
		return -ENOMEM;

	devbasename = basename(devname_copy);

	/* Determine unbind/bind paths based on mode transition */
	if (mode == DAXDEV_MODE_FAMFS) {
		unbind_drv = "device_dax";
		bind_drv = "fsdev_dax";
	} else {
		unbind_drv = "fsdev_dax";
		bind_drv = "device_dax";
	}

	if (verbose)
		printf("%s: Changing mode from %s to %s\n", __func__,
		       daxdev_mode_string(current_mode),
		       daxdev_mode_string(mode));

	snprintf(unbind_path, sizeof(unbind_path),
		 "/sys/bus/dax/drivers/%s/unbind", unbind_drv);
	snprintf(bind_path, sizeof(bind_path),
		 "/sys/bus/dax/drivers/%s/bind", bind_drv);

	for (i = 0; i < max_retries; i++) {
		rc = 0;

		/* Unbind from current driver */
		fp = fopen(unbind_path, "w");
		if (!fp) {
			rc = -errno;
			if (errno == EBUSY)
				goto retry;
			fprintf(stderr, "%s: failed to open %s: %s\n",
				__func__, unbind_path, strerror(errno));
			if (errno == EACCES || errno == EPERM)
				fprintf(stderr,
					"%s: switching dax drivers requires root; try running with sudo\n",
					__func__);
			goto out;
		}
		if (fprintf(fp, "%s", devbasename) < 0) {
			rc = -errno;
			fclose(fp);
			if (errno == EBUSY)
				goto retry;
			fprintf(stderr, "%s: failed to write to %s: %s\n",
				__func__, unbind_path, strerror(errno));
			goto out;
		}
		fclose(fp);

		/* Bind to new driver */
		fp = fopen(bind_path, "w");
		if (!fp) {
			rc = -errno;
			if (errno == EBUSY)
				goto retry;
			fprintf(stderr, "%s: failed to open %s: %s\n",
				__func__, bind_path, strerror(errno));
			goto out;
		}
		if (fprintf(fp, "%s", devbasename) < 0) {
			rc = -errno;
			fclose(fp);
			if (errno == EBUSY)
				goto retry;
			fprintf(stderr, "%s: failed to write to %s: %s\n",
				__func__, bind_path, strerror(errno));
			goto out;
		}
		fclose(fp);

		/* Verify the mode actually changed */
		current_mode = famfs_get_daxdev_mode(daxdev);
		if (current_mode == mode) {
			/* Success */
			if (verbose)
				printf("%s: Success: mode from from %s to %s\n",
				       __func__,
				       daxdev_mode_string(current_mode),
				       daxdev_mode_string(mode));


			rc = 0;
			goto out;
		}

		/*
		 * Mode didn't change - this can happen if unbind was silently
		 * blocked due to an active holder (kernel logs this but sysfs
		 * write still returns success). Retry with backoff.
		 */
		rc = -EBUSY;
		/* fall through to retry */

retry:
		
		if (i < max_retries - 1) {
			if (verbose)
				printf("%s: retry %d (mode=%s, target=%s)\n",
				       __func__, i + 1,
				       daxdev_mode_string(current_mode),
				       daxdev_mode_string(mode));

			usleep(delay_ms * 1000);
			delay_ms *= 2; /* exponential backoff */
		}
	}

	/* All retries exhausted */
	current_mode = famfs_get_daxdev_mode(daxdev);
	fprintf(stderr, "%s: failed after %d retries; expected %s but got %s\n",
		__func__, max_retries,
		(mode == DAXDEV_MODE_FAMFS) ? "fsdev_dax" : "device_dax",
		(current_mode == DAXDEV_MODE_FAMFS) ? "fsdev_dax" :
		(current_mode == DAXDEV_MODE_DEVICE_DAX) ? "device_dax" :
		"unknown");

out:
	free(devname_copy);
	return rc;
}
