// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023 Micron Technology, Inc.  All rights reserved.
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

typedef __u64 u64;

#include "famfs_ioctl.h"
#include "famfs_lib.h"

void
print_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Create a famfs file system:\n"
	       "    %s [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0\n"
	       "\n"
	       "Arguments\n"
	       "    -h|-?      - Print this message\n"
	       "    -f|--force - Will create the file system even if there is allready a superblock\n"
	       "    -k|--kill  - Will 'kill' the superblock (also requires -f)\n"
	       "\n",
	       progname);
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
	{0, 0, 0, 0}
};

int
main(int argc, char *argv[])
{
	int c;

	int arg_ct = 0;
	char *daxdev = NULL;
	int force = 0;

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+fkh?",
				global_options, &optind)) != EOF) {
		arg_ct++;
		switch (c) {
		case 'k':
			/* kill the superblock on the device */
			kill_super++;
			printf("kill_super: %d\n", kill_super);
			break;
		case 'f':
			force++;
			break;

		case 'h':
		case '?':
			print_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	assert(optind < argc);

	/* TODO: multiple devices? */
	daxdev = argv[optind++];

	return famfs_mkfs(daxdev, kill_super, force);
}
