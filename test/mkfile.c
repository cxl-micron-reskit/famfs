
#include <stdio.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

typedef __u64 u64;

#include "../tagfs/tagfs_ioctl.h"

void
print_usage(int   argc,
	    char *argv[])
{
	printf("\n"
	       "Usage: %s -n <num_extents> -a <hpa> -l <len> [-h <hpa> -l <len> ... ] <filename>\n"
		);
}

int verbose_flag = 0;

struct option global_options[] = {
	/* These options set a flag. */
	{"address",     required_argument, &verbose_flag,  'a' },
	{"length",      required_argument, &verbose_flag,  'l'},
	{"num_extents", required_argument,             0,  'n'},
	{"filename",    required_argument,             0,  'f'},
	/* These options don't set a flag.
	   We distinguish them by their indices. */
	/*{"dryrun",       no_argument,       0, 'n'}, */
	{0, 0, 0, 0}
};

int
main(int argc,
     char *argv[])
{
	struct tagfs_ioc_map filemap;
	struct tagfs_user_extent *ext_list;
	int c, i, rc, fd;
	char *filename = NULL;

	int num_extents = 0;
	int cur_extent  = 0;

	size_t ext_size;
	size_t fsize = 0;

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+n:a:l:f:h?",
				global_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'n':
			num_extents = atoi(optarg);
			if (num_extents > 0) {
				ext_list = calloc(num_extents, sizeof(*ext_list));
				filemap.ext_list = ext_list;
				filemap.ext_list_count = num_extents;
			} else {
				printf("Specify at least 1 extent\n");
				exit(-1);
			}
			break;

		case 'a':
			if (num_extents == 0) {
				printf("Must specify num_extents before address\n");
				exit(-1);
			}
			ext_list[cur_extent].hpa = strtoull(optarg, 0, 0);
			/* update cur_extent if we already have len */
			if (ext_list[cur_extent].len)
				cur_extent++;
			break;
			
		case 'l':
			if (num_extents == 0) {
				printf("Must specify num_extents before length\n");
				exit(-1);
			}
			ext_size = strtoull(optarg, 0, 0);
			ext_list[cur_extent].len = ext_size;
			fsize += ext_size;

			/* update cur_extent if we already have hpa */
			if (ext_list[cur_extent].hpa)
				cur_extent++;
			break;

		case 'f':
			filename = optarg;
			printf("filename: %s\n", filename);
			/* TODO: make sure filename is in a tagfs file system */
			break;

		case 'h':
		case '?':
			print_usage(argc, argv);
			return 0;

		default:
			return -1;
		}
	}

	printf("%d extents specified:\n", num_extents);
	printf("Total size: %ld\n", fsize);
	filemap.file_size = fsize;
	for (i=0; i<num_extents; i++)
		printf("\t%p\t%ld\n", ext_list[i].hpa, ext_list[i].len);


	if (filename == NULL) {
		printf("Must supply filename\n");
		exit(-1);
	}
	fd = open(filename, O_RDWR | O_CREAT, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		printf("open/create failed; rc %d errno %d\n", rc, errno);
		exit(-1);
	}
	rc = ioctl(fd, MCIOC_MAP_CREATE, &filemap);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
	}
	close(rc);
}
