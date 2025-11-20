// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
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
	       "Arguments:\n"
	       "    -h|-?      - Print this message\n"
	       "    -f|--force - Will create the file system even if there is already a valid superblock\n"
	       "    -k|--kill  - Will 'kill' existing superblock (also requires -f)\n"
	       "    -l|--loglen <loglen> - Default loglen: 8 MiB\n"
	       "                           Valid range: >= 8 MiB\n"
	       "\n",
	       progname, progname);
}

int verbose_flag;
int kill_super;

struct option global_options[] = {
	/* These options set a flag. */
	{"force",       no_argument,                   0,  'f'},
	/* These options don't set a flag.
	 * We distinguish them by their indices.
	 */
	{"kill",        no_argument,       &kill_super,    'k'},
	{"loglen",      required_argument, 0,              'l'},
	{"nodax",       no_argument,       0,              'D'},
	{0, 0, 0, 0}
};

int
main(int argc, char *argv[])
{
	int c;
	int rc = 0;
	int force = 0;
	int nodax = 0;
	char *daxdev = NULL;
	u64 loglen = 0x800000;

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+fkl:Dh?",
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
		case 'h':
		case '?':
			print_usage(argc, argv);
			return 0;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "mkfs.famfs: must specify memory device\n");
		return -1;
	}

	/* TODO: multiple devices? */
	daxdev = argv[optind++];

	famfs_log_enable_syslog("famfs", LOG_PID | LOG_CONS, LOG_DAEMON);
	famfs_log(FAMFS_LOG_NOTICE, "Starting famfs mkfs on device %s", daxdev);

	if (!nodax)
		rc = famfs_mkfs(daxdev, loglen, kill_super, force);
	else
		rc = famfs_mkfs_via_dummy_mount(daxdev, loglen,
						kill_super, force);

	if (rc == 0)
		famfs_log(FAMFS_LOG_NOTICE,
			  "mkfs %s command successful on device %s",
			  (kill_super && force) ? "-k -f " : "", daxdev);
	else
		famfs_log(FAMFS_LOG_ERR, "mkfs failed on device %s", daxdev);

	famfs_log_close_syslog();
	return rc;
}
