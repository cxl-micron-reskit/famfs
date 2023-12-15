// SPDX-License-Identifier: GPL-2.0

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
	       "Create one or more HPA based extent:\n"
	       "    %s -n <num_extents> -o <hpa> -l <len> [-o <hpa> -l <len> ... ] <filename>\n"
	       "\n", progname);
	printf(
	       "Create one or more dax-based extents:"
	       "    %s --daxdev <daxdev> -n <num_extents> -o <offset> -l <len> [-o <offset> -l <len> ... ] <filename>\n"
	       "\n", progname);
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
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

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

	if (optind >= argc) {
		fprintf(stderr, "Must specify at least one dax device\n");
		return -1;
	}
	/* TODO: multiple devices? */
	daxdev = argv[optind++];

	return famfs_mkfs(daxdev, kill_super, force);
}
