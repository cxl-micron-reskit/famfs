
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

#include "../tagfs/tagfs_ioctl.h"
#include "tagfs_lib.h"

void
print_usage(int   argc,
	    char *argv[])
{
	unsigned char *progname = argv[0];

	printf("\n"
	       "Create one or more HPA based extent:\n"
	       "    %s -n <num_extents> -o <hpa> -l <len> [-o <hpa> -l <len> ... ] <filename>\n"
	       "\n", progname);
	printf(
	       "Create one or more dax-based extents:"
	       "    %s --daxdev <daxdev> -n <num_extents> -o <offset> -l <len> [-o <offset> -l <len> ... ] <filename>\n"
	       "\n", progname);
}

int verbose_flag = 0;

struct option global_options[] = {
	/* These options set a flag. */
	{"daxdev",      required_argument,             0,  'D'},
	{"fsdaxdev",    required_argument,             0,  'F'},
	{"force",       no_argument,                   0,  'f'},
	/* These options don't set a flag.
	   We distinguish them by their indices. */
	/*{"dryrun",       no_argument,       0, 'n'}, */
	{0, 0, 0, 0}
};

int
main(int argc,
     char *argv[])
{
	struct tagfs_extent *ext_list;
	int c, i, rc, fd;
	char *filename = NULL;

	int num_extents = 0;
	int cur_extent  = 0;

	size_t ext_size;
	size_t fsize = 0;
	int arg_ct = 0;
	enum extent_type type = HPA_EXTENT;
	unsigned char *daxdev = NULL;
	int force = 0;

	char *sb_buf;
	struct tagfs_superblock *sb;
	char *log_buf;
	struct tagfs_log *tagfs_logp;
	size_t devsize;

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+D:F:fh?",
				global_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {
		case 'F':
		case 'D': {
			size_t len = 0;
			struct stat devstat;

			/* Must be first argument */
			if (arg_ct != 1) {
				fprintf(stderr, "--daxdev must be the first argument\n");
				exit(-1);
			}
			daxdev = optarg;
			len = strlen(daxdev);
			if (len >= TAGFS_DEVNAME_LEN) {
				fprintf(stderr, "dax devname too long; increate TAGFS_DEVNAME_LEN (%d/%d)\n",
					len, TAGFS_DEVNAME_LEN);
				return -1;
			}

			if (stat(daxdev, &devstat) == -1) {
				fprintf(stderr, "unable to stat special file: %s\n", filename);
				return -1;
			}


			if (c == 'F') {
				if (!S_ISBLK(devstat.st_mode)) {
					fprintf(stderr,
						"FSDAX special file (%s) is not a block device\n",
						daxdev);
				}
				type = FSDAX_EXTENT;
			}
			else if (c == 'D') {
				if (!S_ISCHR(devstat.st_mode)) {
					fprintf(stderr,
						"FSDAX special file (%s) is not a block device\n",
						daxdev);
				}
				type = DAX_EXTENT;
			}
			else {
				fprintf(stderr, "%s is not a device special file\n", daxdev);
				return -1;
			}

			break;
		}
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

	rc = tagfs_get_device_size(daxdev, &devsize);
	if (rc)
		return -1;

	fd = open(daxdev, O_RDWR, 0);
	if (fd < 0) {
		fprintf(stderr, "open/create failed; rc %d errno %d\n", rc, errno);
		exit(-1);
	}

	sb_buf = mmap (0, TAGFS_SUPERBLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (sb_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap device %s\n", daxdev);
		return -1;
	}
	sb = (struct tagfs_superblock *)sb_buf;
	if (sb->ts_magic == TAGFS_SUPER_MAGIC && !force) {
		fprintf(stderr, "Device %s already has a tagfs superblock\n", daxdev);
		return -1;
	}


	log_buf = mmap (0, TAGFS_LOG_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, TAGFS_LOG_OFFSET);
	if (log_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap device %s\n", daxdev);
		return -1;
	}
	tagfs_logp = (struct tagfs_log *)log_buf;

	memset(sb, 0, TAGFS_SUPERBLOCK_SIZE); /* Zero the memory up to the log */

	sb->ts_magic = TAGFS_SUPER_MAGIC;
	sb->ts_version = TAGFS_CURRENT_VERSION;
	sb->ts_log_offset = TAGFS_LOG_OFFSET;
	sb->ts_crc = 0; /* TODO: calculate and check crc */

	/* Configure the first daxdev */
	sb->ts_num_daxdevs = 1;
	sb->ts_devlist[1].dd_size = devsize;
	strncpy(sb->ts_devlist[1].dd_daxdev, daxdev, TAGFS_DEVNAME_LEN);

	/* Zero and setup the log */
	memset(tagfs_logp, 0, TAGFS_LOG_LEN);
	tagfs_logp->tagfs_log_magic      = TAGFS_LOG_MAGIC;
	tagfs_logp->tagfs_log_len        = TAGFS_LOG_LEN;
	tagfs_logp->tagfs_log_next_seqnum    = 99;
	tagfs_logp->tagfs_log_next_index = 0;
	tagfs_logp->tagfs_log_last_index =
		((TAGFS_LOG_LEN
		  - offsetof(struct tagfs_log, entries)) /sizeof(struct tagfs_log_entry)) ;

	print_fsinfo(sb, tagfs_logp, 1);
	close(rc);
}
