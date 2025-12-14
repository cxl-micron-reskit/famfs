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

static struct daxctl_dev *
find_dax_by_name(struct daxctl_ctx *ctx, const char *want)
{
	struct daxctl_region *region;

	daxctl_region_foreach(ctx, region) {
		struct daxctl_dev *dev;
		daxctl_dev_foreach(region, dev) {
			const char *name = daxctl_dev_get_devname(dev); /* "daxX.Y" */
			if (name && strcmp(name, want) == 0)
				return dev;
		}
	}
	return NULL;
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
 * Returns 0 on success, negative errno on failure.
 */
int famfs_set_daxdev_mode(const char *daxdev, enum famfs_daxdev_mode mode)
{
	enum famfs_daxdev_mode current_mode;
	char unbind_path[PATH_MAX];
	char bind_path[PATH_MAX];
	const char *unbind_drv;
	const char *bind_drv;
	char *devname_copy;
	char *devbasename;
	FILE *fp;
	int rc = 0;

	if (!daxdev)
		return -EINVAL;

	if (mode != DAXDEV_MODE_DEVICE_DAX && mode != DAXDEV_MODE_FAMFS)
		return -EINVAL;

	/* Check current mode */
	current_mode = famfs_get_daxdev_mode(daxdev);
	if (current_mode == mode)
		return 0; /* Already in requested mode */

	if (current_mode == DAXDEV_MODE_UNKNOWN)
		return -ENODEV;

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

	snprintf(unbind_path, sizeof(unbind_path),
		 "/sys/bus/dax/drivers/%s/unbind", unbind_drv);
	snprintf(bind_path, sizeof(bind_path),
		 "/sys/bus/dax/drivers/%s/bind", bind_drv);

	/* Unbind from current driver */
	fp = fopen(unbind_path, "w");
	if (!fp) {
		rc = -errno;
		fprintf(stderr, "%s: failed to open %s: %s\n",
			__func__, unbind_path, strerror(errno));
		goto out;
	}
	if (fprintf(fp, "%s", devbasename) < 0) {
		rc = -errno;
		fprintf(stderr, "%s: failed to write to %s: %s\n",
			__func__, unbind_path, strerror(errno));
		fclose(fp);
		goto out;
	}
	fclose(fp);

	/* Bind to new driver */
	fp = fopen(bind_path, "w");
	if (!fp) {
		rc = -errno;
		fprintf(stderr, "%s: failed to open %s: %s\n",
			__func__, bind_path, strerror(errno));
		goto out;
	}
	if (fprintf(fp, "%s", devbasename) < 0) {
		rc = -errno;
		fprintf(stderr, "%s: failed to write to %s: %s\n",
			__func__, bind_path, strerror(errno));
		fclose(fp);
		goto out;
	}
	fclose(fp);

out:
	free(devname_copy);
	return rc;
}

int famfs_bounce_daxdev(const char *name, int verbose)
{
	struct daxctl_ctx *ctx = NULL;
	struct daxctl_dev *dev = NULL;
	char *devname = strdup(name);
	char *devbasename = basename(devname);
	const char *realdevname;
	int rc = 0;
	(void)verbose;

	rc = daxctl_new(&ctx);
	if (rc) {
		fprintf(stderr, "%s: daxctl_new() failed: %s\n",
			__func__, strerror(-rc));
		goto err_out;
	}

	dev = find_dax_by_name(ctx, devbasename);
	if (!dev) {
		fprintf(stderr, "%s: No such DAX device: %s\n",
			__func__, devname);
		rc = -ENODEV;
		goto err_out;
	}

	/* Kinda circular, but correct */
	realdevname = daxctl_dev_get_devname(dev);

	/* Step 1: Disable (no-op if already disabled) */
	rc = daxctl_dev_disable(dev);
	if (rc) {
		fprintf(stderr, "%s: failed to disable %s (errno=%d)\n",
			__func__, realdevname, errno);
		rc = -errno;
		goto err_out;
	}
	printf("%s: disabled\n", realdevname);

	/* Step 2: Enable in devdax mode */
	rc = daxctl_dev_enable_devdax(dev);
	if (rc) {
		fprintf(stderr, "%s: dax_dev_enable(%s) failed (errno=%d)\n",
			__func__, realdevname, errno);
		rc = -errno;
		goto err_out;
	}

	/* Optional: verify enabled */
	if (!daxctl_dev_is_enabled(dev)) {
		fprintf(stderr,
			"%s: daxctl_dev_is_enabled(%s) errno=%d\n",
			__func__, realdevname, errno);
		rc = -1;
		goto err_out;
	}

	printf("%s: re-enabled in devdax mode\n", realdevname);

err_out:
	if (ctx)
		daxctl_unref(ctx);
	free(devname);
	return rc;
}

#ifdef STANDALONE
int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s daxX.Y | /dev/daxX.Y\n", argv[0]);
		return 2;
	}

	arg_name = basename_dev(argv[1]);

	return famfs_bounce_daxdev(arg_name);
}
#endif
