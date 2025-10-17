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

#include <daxctl/libdaxctl.h>

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
