
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

#include <sys/types.h>
#include <sys/stat.h>

typedef __u64 u64;

#include "../tagfs/tagfs_ioctl.h"

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
	{"address",     required_argument, &verbose_flag,  'o' },
	{"length",      required_argument, &verbose_flag,  'l'},
	{"num_extents", required_argument,             0,  'n'},
	{"filename",    required_argument,             0,  'f'},
	{"daxdev",      required_argument,             0,  'D'},
	{"fsdaxdev",    required_argument,             0,  'F'},
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

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+n:o:l:f:D:F:h?",
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
			if (len <= 0 || len >= sizeof(filemap.devname)) {
				fprintf(stderr, "Invalid dax device string: (%s)\n", daxdev);
				exit(-1);
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

			strncpy(filemap.devname, daxdev, len);
			filemap.devno = devstat.st_rdev; /* Device number (dev_t) for the dax dev */
			break;
		}
		case 'n':
			num_extents = atoi(optarg);
			if (num_extents > 0) {
				ext_list = calloc(num_extents, sizeof(*ext_list));
				filemap.ext_list = ext_list;
				filemap.ext_list_count = num_extents;
			} else {
				fprintf(stderr, "Specify at least 1 extent\n");
				exit(-1);
			}
			break;

		case 'o':
			if (num_extents == 0) {
				fprintf(stderr, "Must specify num_extents before address or offset\n");
				exit(-1);
			}
			ext_list[cur_extent].offset = strtoull(optarg, 0, 0);

			/* update cur_extent if we already have len */
			if (ext_list[cur_extent].len)
				cur_extent++;
			break;
			
		case 'l':
			if (num_extents == 0) {
				fprintf(stderr, "Must specify num_extents before length\n");
				exit(-1);
			}
			ext_size = strtoull(optarg, 0, 0);
			if (ext_size <= 0) {
				fprintf(stderr, "invalid extent size %ld\n", ext_size);
				exit(-1);
			}
			ext_list[cur_extent].len = ext_size;
			fsize += ext_size;

			/* update cur_extent if we already have offset */
			if (ext_list[cur_extent].offset)
				cur_extent++;
			break;

		case 'f': {
			filename = optarg;
			printf("filename: %s\n", filename);
			/* TODO: make sure filename is in a tagfs file system */
			break;
		}
		case 'h':
		case '?':
			print_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	printf("%d extents specified:\n", num_extents);
	printf("Total size: %ld\n", fsize);
	filemap.file_size   = fsize;
	filemap.extent_type = type;
	for (i=0; i<num_extents; i++)
		printf("\t%p\t%ld\n", ext_list[i].offset, ext_list[i].len);


	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		exit(-1);
	}
	fd = open(filename, O_RDWR | O_CREAT, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "open/create failed; rc %d errno %d\n", rc, errno);
		exit(-1);
	}
	rc = ioctl(fd, TAGFSIOC_MAP_CREATE, &filemap);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		unlink(filename);
	}
	close(rc);
}