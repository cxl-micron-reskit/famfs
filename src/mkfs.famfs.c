// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2026 Micron Technology, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <strings.h>
#include <stddef.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/famfs_ioctl.h>

#include "famfs.h"
#include "famfs_lib.h"

void
print_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "Create a famfs file system:\n"
	       "    %s [args] <memdevice>  # Example memdevice: /dev/dax0.0\n"
	       "\n"
	       "Create a famfs file system with a 256MiB log\n"
	       "    %s --loglen 256m /dev/dax0.0\n"
	       "\n"
	       "Note: You must be root to run mkfs.famfs\n"
	       "\n"
	       "Arguments:\n"
	       "    -h|-?             - Print this message\n"
	       "    -f|--force        - Will create the file system even if there is already a valid superblock\n"
	       "    -k|--kill         - Will 'kill' existing superblock (also requires -f)\n"
	       "    -l|--loglen <len> - Default loglen: 8 MiB; valid range: >= 8 MiB\n"
	       "    -U|--alloc-unit <au> - Allocation unit: 4k or 2m (default: 2m)\n"
	       "    --4k              - Shortcut for --alloc-unit 4k (also --4K)\n"
	       "    -M|--set-daxmode  - Switch daxdev to famfs mode if needed (kernel >= 7.0 only).\n"
	       "                        Without this flag, mkfs fails with a clear message if the\n"
	       "                        device is not already in famfs mode. The device is left in\n"
	       "                        famfs mode after the operation.\n"
	       "    -F|--nofuse       - Use the standalone famfs v1 path for the internal dummy\n"
	       "                        mount (overrides FAMFS_MODE and auto-detection).\n"
	       "    --fuse            - Use the fuse path for the internal dummy mount\n"
	       "                        (overrides FAMFS_MODE and auto-detection).\n"
	       "\n",
	       progname, progname);
}

int verbose_flag;
int kill_super;

/* Long-only option value for --fuse (-f is already taken by --force) */
#define MKFS_OPT_FUSE 256
/* Long-only option value for the --4k / --4K alloc-unit shortcut */
#define MKFS_OPT_4K   257

/*
 * Parse an --alloc-unit value. Accepts "4k"/"4096" (4 KiB) and "2m"/"2097152"
 * (2 MiB), case-insensitively. Returns 0 and sets *au_out on success, -1 on an
 * unrecognized value. These are the only allocation units famfs supports.
 */
static int
parse_alloc_unit(const char *s, u64 *au_out)
{
	if (!strcasecmp(s, "4k") || !strcmp(s, "4096")) {
		*au_out = 4096;
		return 0;
	}
	if (!strcasecmp(s, "2m") || !strcmp(s, "2097152")) {
		*au_out = FAMFS_ALLOC_UNIT;
		return 0;
	}
	return -1;
}

struct option global_options[] = {
	/* These options set a flag. */
	{"force",       no_argument,                   0,  'f'},
	/* These options don't set a flag.
	 * We distinguish them by their indices.
	 */
	{"kill",        no_argument,       &kill_super,    'k'},
	{"loglen",      required_argument, 0,              'l'},
	{"nodax",       no_argument,       0,              'D'},
	{"set-daxmode", no_argument,       0,              'M'},
	{"nofuse",      no_argument,       0,              'F'},
	{"fuse",        no_argument,       0,  MKFS_OPT_FUSE},
	{"verbose",     no_argument,       0,              'v'},
	{"alloc-unit",  required_argument, 0,              'U'},
	{"4k",          no_argument,       0,  MKFS_OPT_4K},
	{"4K",          no_argument,       0,  MKFS_OPT_4K},
	{0, 0, 0, 0}
};

int
main(int argc, char *argv[])
{
	int c;
	int rc = 0;
	int force = 0;
	int nodax = 0;
	int verbose = 0;
	bool set_daxmode = false;
	char *daxdev = NULL;
	u64 loglen = 0x800000;
	u64 alloc_unit = FAMFS_ALLOC_UNIT; /* default 2 MiB; --4k selects 4 KiB */

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+fFkl:DMU:h?",
				global_options, &optind)) != EOF) {
		char *endptr;
		s64 mult;

		switch (c) {
		case 'k':
			/* kill the superblock on the device */
			kill_super++;
			printf("kill superblock: %d\n", kill_super);
			break;
		case 'f':
			force++;
			break;
		case 'l':
			loglen = strtoull(optarg, &endptr, 0);
			mult = get_multiplier(endptr);
			if (mult > 0)
				loglen *= mult;
			printf("loglen: %lld\n", loglen);
			break;
		case 'D':
			nodax = 1;
			break;
		case 'M':
			set_daxmode = true;
			break;
		case 'F':
			/* Pin the internal dummy mount to standalone v1.
			 * famfs_select_mode() reads FAMFS_MODE; overwrite it so
			 * an explicit flag beats any inherited env value. */
			setenv("FAMFS_MODE", "v1", 1);
			break;
		case MKFS_OPT_FUSE:
			setenv("FAMFS_MODE", "fuse", 1);
			break;
		case 'U':
			if (parse_alloc_unit(optarg, &alloc_unit)) {
				fprintf(stderr,
					"mkfs.famfs: invalid alloc unit '%s' "
					"(valid: 4k or 2m)\n", optarg);
				return 1;
			}
			break;
		case MKFS_OPT_4K:
			alloc_unit = 4096;
			break;
		case 'v':
			verbose++;
			break;
		case 'h':
		case '?':
			print_usage(argc, argv);
			return 0;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "mkfs.famfs: must specify memory device\n");
		return 1;
	}

	/* TODO: multiple devices? */
	daxdev = argv[optind++];

	if (geteuid() != 0) {
		fprintf(stderr, "mkfs.famfs: must run as root\n");
		return 1;
	}

	famfs_log_enable_syslog("famfs", LOG_PID | LOG_CONS, LOG_DAEMON);
	famfs_log(FAMFS_LOG_NOTICE, "Starting famfs mkfs on device %s", daxdev);

	rc = famfs_mkfs(daxdev, loglen, alloc_unit, kill_super, nodax, force, set_daxmode, verbose);
	if (rc == 0)
		famfs_log(FAMFS_LOG_NOTICE,
			  "mkfs %s command successful on device %s",
			  (kill_super && force) ? "-k -f " : "", daxdev);
	else
		famfs_log(FAMFS_LOG_ERR, "mkfs failed on device %s", daxdev);

	famfs_log_close_syslog();
	return (rc) ? 2:0;
}
