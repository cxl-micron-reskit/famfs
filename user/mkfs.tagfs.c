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

#include "tagfs_ioctl.h"
#include "tagfs_lib.h"

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
	{"daxdev",      required_argument,             0,  'D'},
	{"fsdaxdev",    required_argument,             0,  'F'},
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
	int c, rc;

	int arg_ct = 0;
	enum extent_type type = HPA_EXTENT;
	char *daxdev = NULL;
	int force = 0;

	struct famfs_superblock *sb;
	struct famfs_log *famfs_logp;
	size_t devsize;

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

	rc = famfs_get_device_size(daxdev, &devsize, &type);
	if (rc)
		return -1;

	printf("devsize: %ld\n", devsize);

	rc = famfs_mmap_superblock_and_log_raw(daxdev, &sb, &famfs_logp, 0 /* read/write */);
	if (rc)
		return -1;

	if ((famfs_check_super(sb) == 0) && !force) {
		fprintf(stderr, "Device %s already has a famfs superblock\n", daxdev);
		return -1;
	}

	memset(sb, 0, FAMFS_SUPERBLOCK_SIZE); /* Zero the memory up to the log */

	if (kill_super) {
		printf("Famfs superblock killed\n");
		sb->ts_magic      = 0;
		return 0;
	}
	sb->ts_magic      = FAMFS_SUPER_MAGIC;

	sb->ts_version    = FAMFS_CURRENT_VERSION;
	sb->ts_log_offset = FAMFS_LOG_OFFSET;
	sb->ts_log_len    = FAMFS_LOG_LEN;
	famfs_uuidgen(&sb->ts_uuid);
	sb->ts_crc = 0; /* TODO: calculate and check crc */

	/* Configure the first daxdev */
	sb->ts_num_daxdevs = 1;
	sb->ts_devlist[0].dd_size = devsize;
	strncpy(sb->ts_devlist[0].dd_daxdev, daxdev, FAMFS_DEVNAME_LEN);

	/* Zero and setup the log */
	memset(famfs_logp, 0, FAMFS_LOG_LEN);
	famfs_logp->famfs_log_magic      = FAMFS_LOG_MAGIC;
	famfs_logp->famfs_log_len        = FAMFS_LOG_LEN;
	famfs_logp->famfs_log_next_seqnum    = 99;
	famfs_logp->famfs_log_next_index = 0;
	famfs_logp->famfs_log_last_index =
		((FAMFS_LOG_LEN - offsetof(struct famfs_log, entries))
		 / sizeof(struct famfs_log_entry));

	famfs_fsck_scan(sb, famfs_logp, 0);
	close(rc);
	return 0;
}
