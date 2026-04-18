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
	case DAXDEV_MODE_UNBOUND:
		return "unbound";
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
	char devpath[PATH_MAX];
	char syspath[PATH_MAX];
	char linkbuf[PATH_MAX];
	char *devname_copy;
	char *devbasename;
	char *driver_name;
	ssize_t len;

	if (!daxdev)
		return DAXDEV_MODE_UNKNOWN;

	/* Get basename of device path (handles "/dev/dax1.0" and "dax1.0") */
	devname_copy = strdup(daxdev);
	if (!devname_copy)
		return DAXDEV_MODE_UNKNOWN;

	devbasename = basename(devname_copy);

	/* Verify the device exists in sysfs before going further */
	snprintf(devpath, sizeof(devpath),
		 "/sys/bus/dax/devices/%s", devbasename);
	if (access(devpath, F_OK) != 0) {
		fprintf(stderr, "%s: dax device '%s' not found in sysfs (%s)\n",
			__func__, devbasename, devpath);
		free(devname_copy);
		return DAXDEV_MODE_UNKNOWN;
	}

	/* Construct sysfs driver symlink path */
	if (snprintf(syspath, sizeof(syspath), "%s/driver", devpath)
			>= (int)sizeof(syspath)) {
		fprintf(stderr, "%s: sysfs path too long for '%s'\n",
			__func__, daxdev);
		free(devname_copy);
		return DAXDEV_MODE_UNKNOWN;
	}

	/* Read the symlink */
	len = readlink(syspath, linkbuf, sizeof(linkbuf) - 1);
	free(devname_copy);

	if (len < 0) {
		if (errno == ENOENT) {
			fprintf(stderr,
				"%s: '%s' exists but has no driver bound\n",
				__func__, daxdev);
			return DAXDEV_MODE_UNBOUND;
		}
		return DAXDEV_MODE_UNKNOWN;
	}

	/* Guard against a truncated readlink result */
	if (len == (ssize_t)(sizeof(linkbuf) - 1)) {
		fprintf(stderr, "%s: driver symlink path truncated for '%s'\n",
			__func__, daxdev);
		return DAXDEV_MODE_UNKNOWN;
	}

	linkbuf[len] = '\0';

	/* Get basename of the link target (e.g., "device_dax" or "fsdev_dax") */
	driver_name = basename(linkbuf);

	if (strcmp(driver_name, "device_dax") == 0)
		return DAXDEV_MODE_DEVICE_DAX;
	else if (strcmp(driver_name, "fsdev_dax") == 0)
		return DAXDEV_MODE_FAMFS;

	fprintf(stderr, "%s: unrecognized driver '%s' for '%s'\n",
		__func__, driver_name, daxdev);
	return DAXDEV_MODE_UNKNOWN;
}

/**
 * famfs_set_daxdev_mode() - Change the mode of a dax device via libdaxctl
 * @daxdev: Path to the dax device (e.g., "/dev/dax1.0" or "dax1.0")
 * @mode:   Target mode (DAXDEV_MODE_DEVICE_DAX or DAXDEV_MODE_FAMFS)
 *
 * Uses libdaxctl to properly reconfigure the device mode, including
 * disabling the device, changing the mode attribute, and re-enabling
 * with the correct driver.  Raw sysfs bind/unbind is insufficient because
 * the kernel driver refuses to bind unless the device's configured mode
 * matches the driver (and returns an error that fclose() silently drops).
 *
 * Returns 0 on success, negative errno on failure.
 */
int famfs_set_daxdev_mode(
	const char *daxdev,
	enum famfs_daxdev_mode mode,
	int verbose)
{
	struct daxctl_region *region;
	struct daxctl_ctx *ctx;
	struct daxctl_dev *dev;
	char *devname_copy;
	const char *devbasename;
	int found = 0;
	int rc = -ENODEV;

	if (!daxdev)
		return -EINVAL;
	if (mode != DAXDEV_MODE_DEVICE_DAX && mode != DAXDEV_MODE_FAMFS) {
		fprintf(stderr, "%s: invalid mode %d\n", __func__, mode);
		return -EINVAL;
	}

	devname_copy = strdup(daxdev);
	if (!devname_copy)
		return -ENOMEM;
	devbasename = basename(devname_copy);

	if (daxctl_new(&ctx) < 0) {
		fprintf(stderr, "%s: failed to create daxctl context\n",
			__func__);
		free(devname_copy);
		return -ENOMEM;
	}

	if (verbose)
		printf("%s: switching %s to %s mode via libdaxctl\n",
		       __func__, devbasename, daxdev_mode_string(mode));

	daxctl_region_foreach(ctx, region) {
		daxctl_dev_foreach(region, dev) {
			if (strcmp(daxctl_dev_get_devname(dev), devbasename) != 0)
				continue;

			found = 1;

			/*
			 * Mirror the daxctl reconfigure-device pattern:
			 * disable the device (unbind from current driver)
			 * before re-enabling in the target mode.
			 * daxctl_dev_enable_famfs/devdax() alone does NOT
			 * handle the disable step.
			 */
			if (daxctl_dev_is_enabled(dev)) {
				rc = daxctl_dev_disable(dev);
				if (rc < 0) {
					fprintf(stderr,
						"%s: failed to disable %s: %s\n",
						__func__, devbasename,
						strerror(-rc));
					goto done;
				}
			}

			if (mode == DAXDEV_MODE_FAMFS)
				rc = daxctl_dev_enable_famfs(dev);
			else
				rc = daxctl_dev_enable_devdax(dev);

			if (rc < 0)
				fprintf(stderr,
					"%s: failed to enable %s in %s mode: %s\n",
					__func__, devbasename,
					daxdev_mode_string(mode),
					strerror(-rc));
			else if (verbose)
				printf("%s: %s is now in %s mode\n",
				       __func__, devbasename,
				       daxdev_mode_string(mode));
			goto done;
		}
	}
done:
	daxctl_unref(ctx);
	free(devname_copy);

	if (!found) {
		fprintf(stderr, "%s: device %s not found\n",
			__func__, devbasename);
		return -ENODEV;
	}
	return rc;
}

/**
 * famfs_check_or_set_daxmode() - Enforce that a daxdev is in famfs mode.
 *
 * On kernels where famfs mode is not required (< 7.0), this is always a
 * no-op and returns 0.
 *
 * On kernels where famfs mode IS required (>= 7.0):
 *   - If @set_daxmode is true and the device is not already in famfs mode,
 *     switch it.  The device is left in famfs mode after the call.
 *   - If @set_daxmode is false and the device is not already in famfs mode,
 *     print an actionable error and return -EPERM.
 *   - If the device is already in famfs mode, succeed regardless of
 *     @set_daxmode.
 *
 * @daxdev:      Path to dax device (e.g. "/dev/dax0.0")
 * @set_daxmode: true if the caller explicitly requested a mode switch
 * @caller_cmd:  Command name for error messages (e.g. "famfs mount")
 * @verbose:
 */
int famfs_check_or_set_daxmode(
	const char *daxdev,
	bool        set_daxmode,
	const char *caller_cmd,
	int         verbose)
{
	enum famfs_daxdev_mode mode;

	if (!famfs_daxmode_required())
		return 0;

	mode = famfs_get_daxdev_mode(daxdev);

	if (mode == DAXDEV_MODE_FAMFS)
		return 0;

	if (mode == DAXDEV_MODE_UNKNOWN) {
		/* Device not found in sysfs at all */
		fprintf(stderr,
			"%s: dax device %s not found\n",
			caller_cmd, daxdev);
		return -ENODEV;
	}

	/* Device is in device_dax or unbound state; famfs mode is required */
	if (!set_daxmode) {
		fprintf(stderr,
			"\n%s: %s is in %s mode; famfs mode is required"
			" on kernel 7.0+.\n"
			"Re-run with --set-daxmode to switch drivers:\n"
			"  sudo %s --set-daxmode %s ...\n"
			"Warning: mode switching may take significant time on"
			" large devices.\n"
			"The device will remain in famfs mode after the"
			" operation.\n\n",
			caller_cmd, daxdev, daxdev_mode_string(mode),
			caller_cmd, daxdev);
		return -EPERM;
	}

	return famfs_set_daxdev_mode(daxdev, DAXDEV_MODE_FAMFS, verbose);
}
