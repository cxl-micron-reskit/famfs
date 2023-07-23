
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/ioctl.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/user.h>
#include <sys/param.h> /* MIN()/MAX() */

#include <linux/types.h>
#include <linux/ioctl.h>

typedef __u64 u64;

#include "tagfs_ioctl.h"
#include "tagfs_lib.h"
#include "random_buffer.h"

/* maybe move to internal lib */

char *
xbasename(char *str)
{
        char *s;

        if (!strstr(str, "/"))
                return str;

        s = strrchr(str, '/');
        return s+1;
}


/* Global option related stuff */

int verbose_flag = 0;  /* JG: ignored at the moment */
static int dry_run;

struct option global_options[] = {
	/* These options set a flag. */
	{"verbose",          no_argument, &verbose_flag,  1 },
	{"brief",            no_argument, &verbose_flag,  0 },
	/* These options don't set a flag.
	   We distinguish them by their indices. */
	{"dryrun",       no_argument,       0, 'n'},
	{0, 0, 0, 0}
};

void print_global_opts(void)
{
	int i = 0;

	printf("Global args:\n");
	while (global_options[i].name)
		printf("\t--%s\n", global_options[i++].name);
}

/********************************************************************/

void
tagfs_logplay_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Play the log into a tagfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_logplay(int argc, char *argv[])
{
	int c;
	int arg_ct = 0;
	struct tagfs_log *logp;
	char mpt_out[PATH_MAX];
	size_t log_size;
	void *addr;
	char *filename;
	int lfd;
	int dry_run = 0;

/* XXX can't use any of the same strings as the global args! */
	struct option logplay_options[] = {
		/* These options set a */
		{"dryrun",      required_argument,             0,  'n'},
		{0, 0, 0, 0}
	};

	/* The next stuff on the command line is file names;
	 * err if nothing is left */
	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no files\n");
		tagfs_logplay_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+nh?",
				logplay_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {
		case 'n':
			dry_run++;
			printf("dry_run selected\n");
			break;
		case 'h':
		case '?':
			tagfs_logplay_usage(argc, argv);
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
	filename = argv[optind++];

	lfd = open_log_file_writable(filename, &log_size, mpt_out);
	if (lfd < 0) {
		fprintf(stderr, "%s: failed to open log file for filesystem %s\n",
			__func__, filename);
		return -1;
	}
	addr = mmap(0, log_size, O_RDWR, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file %s", __func__, filename);
		close(lfd);
		return -1;
	}
	close(lfd);
	logp = (struct tagfs_log *)addr;

	tagfs_logplay(logp, mpt_out, dry_run);
	return 0;
}

/********************************************************************/

void
tagfs_mkmeta_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Expose the meta files of a tagfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_mkmeta(int argc, char *argv[])
{
	int c;

	int arg_ct = 0;
	char *daxdev = NULL;
	char *realdaxdev = NULL;

	/* XXX can't use any of the same strings as the global args! */
	struct option mkmeta_options[] = {
		/* These options set a */
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no args\n");
		tagfs_mkmeta_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left */
	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no files\n");
		tagfs_mkmeta_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+h?",
				mkmeta_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			tagfs_mkmeta_usage(argc, argv);
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
	realdaxdev = realpath(daxdev, NULL);
	if (!realdaxdev) {
		fprintf(stderr, "%s: realpath(%s) returned %d\n",
			__func__, realdaxdev, errno);
		return -1;
	}
	tagfs_mkmeta(realdaxdev);
	return 0;
}

/********************************************************************/

void
tagfs_fsck_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Check a tagfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_fsck(int argc, char *argv[])
{
	int c;

	int arg_ct = 0;
	char *daxdev = NULL;

	/* XXX can't use any of the same strings as the global args! */
	struct option fsck_options[] = {
		/* These options set a */
		{"daxdev",      required_argument,             0,  'D'},
		{"fsdaxdev",    required_argument,             0,  'F'},
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no args\n");
		tagfs_fsck_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left */
	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no files\n");
		tagfs_fsck_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+h?",
				fsck_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			tagfs_fsck_usage(argc, argv);
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
	return tagfs_fsck(daxdev, verbose_flag);
}


/********************************************************************/

void
tagfs_cp_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Copy a file into a tagfs file system\n"
	       "    %s <srcfile> <destfile>\n"
	       "\n"
	       "NOTE: you need this tool to copy a file into a tagfs file system,\n"
	       "but the standard \'cp\' can be used to copy FROM a tagfs file system.\n",
	       progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_cp(int argc, char *argv[])
{
	int c, rc;

	int arg_ct = 0;

	char *srcfile;
	char *destfile;

	/* XXX can't use any of the same strings as the global args! */
	struct option cp_options[] = {
		/* These options set a */
		{"filename",    required_argument,             0,  'f'},
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no args\n");
		tagfs_cp_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left */
	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no files\n");
		tagfs_cp_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+h?",
				cp_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			tagfs_cp_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	srcfile = argv[optind++];
	destfile = argv[optind++];

	rc = tagfs_cp(srcfile, destfile);
	printf("tagfs_cp returned %d\n", rc);
	return 0;
}


/********************************************************************/

void
tagfs_getmap_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Get the allocation map of a file:\n"
	       "    %s <filename>\n"
	       "\n", progname);
}

int
do_tagfs_cli_getmap(int argc, char *argv[])
{
	struct tagfs_ioc_map filemap;
	struct tagfs_extent *ext_list;
	int c, i, fd;
	int rc = 0;
	char *filename = NULL;

	int arg_ct = 0;

	/* XXX can't use any of the same strings as the global args! */
       struct option cp_options[] = {
               /* These options set a */
               {0, 0, 0, 0}
       };

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no args\n");
		tagfs_getmap_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left */
	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no files\n");
		tagfs_getmap_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+h?",
				cp_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			tagfs_getmap_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Must specify filename\n");
		return -1;
	}
	filename = argv[optind++];
	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		exit(-1);
	}
	fd = open(filename, O_RDONLY, 0);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s rc %d errno %d\n",
			filename, rc, errno);
		exit(-1);
	}
	rc = ioctl(fd, TAGFSIOC_MAP_GET, &filemap);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		return rc;
	}
	ext_list = calloc(filemap.ext_list_count, sizeof(struct tagfs_extent));
	rc = ioctl(fd, TAGFSIOC_MAP_GETEXT, ext_list);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		return rc;
	}

	printf("File:     %s\n",    filename);
	printf("\tsize:   %ld\n",  filemap.file_size);
	printf("\textents: %ld\n", filemap.ext_list_count);

	for (i=0; i<filemap.ext_list_count; i++) {
		printf("\t\t%llx\t%lld\n", ext_list[i].offset, ext_list[i].len);
	}
	close(rc);

	return 0;
}

/********************************************************************/

void
tagfs_creat_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Create a file backed by one or morespecified extents:\n"
	       "    %s -n <num_extents> -o <hpa> -l <len> [-o <hpa> -l <len> ... ] <filename>\n"
	       "\n"
	       "(the allocation will be logged without regard to whether the "
	       "extents were available\n\n"
	       "Create a file backed by free space:\n"
	       "    %s -s <size> <filename>\n\n"
	       "Create a file containing randomized data from a specific seed:\n"
	       "    %s -s size --randomize --seed <myseed> <filename",
	       progname, progname, progname);
}

int
do_tagfs_cli_creat(int argc, char *argv[])
{
	struct tagfs_simple_extent *ext_list;
	int c, i, rc, fd;
	char *filename = NULL;
	char fullpath[PATH_MAX];

	int num_extents = 0;
	int cur_extent  = 0;

	size_t ext_size;
	size_t fsize = 0;
	size_t ext_list_size = 0;
	int arg_ct = 0;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	s64 seed;
	int randomize = 0;

	/* XXX can't use any of the same strings as the global args! */
	struct option creat_options[] = {
		/* These options set a flag. */
		{"address",     required_argument, &verbose_flag,  'o' },
		{"length",      required_argument, &verbose_flag,  'l'},
		{"num_extents", required_argument,             0,  'n'},
		{"filename",    required_argument,             0,  'f'},
		{"size",        required_argument,             0,  's'},
		{"seed",        required_argument,             0,  'S'},
		{"randomize",   no_argument,                   0,  'r'},
		/* These options don't set a flag.
		   We distinguish them by their indices. */
		/*{"dryrun",       no_argument,       0, 'n'}, */
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli creat: no args\n");
		tagfs_creat_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left */
	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli creat: no files\n");
		tagfs_creat_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+n:o:l:f:s:S:rh?",
				creat_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'n':
			num_extents = atoi(optarg);
			if (num_extents > 0) {
				ext_list = calloc(num_extents, sizeof(*ext_list));
			} else {
				fprintf(stderr, "Specify at least 1 extent\n");
				exit(-1);
			}
			break;

		case 'o':
			if (num_extents == 0) {
				fprintf(stderr,
					"Must specify num_extents before address or offset\n");
				exit(-1);
			}
			ext_list[cur_extent].tagfs_extent_offset = strtoull(optarg, 0, 0);

			/* update cur_extent if we already have len */
			if (ext_list[cur_extent].tagfs_extent_len)
				cur_extent++;
			break;
			
		case 'l':
			if (num_extents == 0) {
				fprintf(stderr,
					"Must specify num_extents before length\n");
				exit(-1);
			}
			ext_size = strtoull(optarg, 0, 0);
			if (ext_size <= 0) {
				fprintf(stderr, "invalid extent size %ld\n",
					ext_size);
				exit(-1);
			}
			ext_list[cur_extent].tagfs_extent_len = ext_size;
			ext_list_size += ext_size;

			/* update cur_extent if we already have offset */
			if (ext_list[cur_extent].tagfs_extent_offset)
				cur_extent++;
			break;

		case 'f': {
			filename = optarg;
			printf("filename: %s\n", filename);
			/* TODO: make sure filename is in a tagfs file system */
			break;
		}
		case 's':
			fsize = strtoull(optarg, 0, 0);
			if (fsize <= 0) {
				fprintf(stderr, "invalid file size %ld\n",
					fsize);
				exit(-1);
			}
			break;

		case 'S':
			seed = strtoull(optarg, 0, 0);
			break;

		case 'r':
			randomize++;
			break;

		case 'h':
			fsize = strtoull(optarg, 0, 0);
			if (fsize <= 0) {
				fprintf(stderr, "invalid file size %ld\n",
					fsize);
				exit(-1);
			}
			break;

		case '?':
			tagfs_creat_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		exit(-1);
	}

	if (num_extents > 0) {
		printf("%d extents specified:\n", num_extents);
		printf("Total size: %ld\n", fsize);
		for (i=0; i<num_extents; i++)
			printf("\t%p\t%lld\n", (void *)ext_list[i].tagfs_extent_offset,
			       ext_list[i].tagfs_extent_len);
	}
	if (ext_list_size && fsize) {
		if (fsize > ext_list_size) {
			fprintf(stderr, "error: fsize(%ld) > ext_list_size (%ld)\n",
				fsize, ext_list_size);
			exit(-1);
		}
	}
	fd = tagfs_file_create(filename, S_IRUSR|S_IWUSR, uid, gid, fsize);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, fullpath);
		exit(-1);
	}

	/* Clean up the filename path. (Can't call realpath until the file exists) */
	if (realpath(filename, fullpath) == NULL) {
		fprintf(stderr, "realpath() unable to rationalize filename %s\n",
			filename);
	}

	if (num_extents > 0) {
		struct tagfs_log *logp;
		char mpt_out[PATH_MAX];
		size_t log_size;
		char *relpath;
		void *addr;
		int lfd;

		/* Extent list was specified, so it's not coming from the official
		 * allocator Log the allocation anyway, which can be used for
		 * testing duplicate allocations, etc. */

		lfd = open_log_file_writable(fullpath, &log_size, mpt_out);
		addr = mmap(0, log_size, O_RDWR, MAP_SHARED, lfd, 0);
		if (addr == MAP_FAILED) {
			fprintf(stderr, "%s: Failed to mmap log file", __func__);
			close(lfd);
			return -1;
		}
		close(lfd);
		logp = (struct tagfs_log *)addr;

		relpath = tagfs_relpath_from_fullpath(mpt_out, fullpath);
		if (!relpath)
			return -EINVAL;

		/* XXX - tagfs_log_file_creation should only be called outside
		 * tagfs_lib.c if we are intentionally doing extent list allocation
		 * bypassing tagfs_lib. This is useful for testing, by generating
		 * problematic extent lists on purpoose...
		 */
		rc = tagfs_log_file_creation(logp, num_extents, ext_list,
					     relpath, O_RDWR, uid, gid, fsize);
		if (rc) {
			fprintf(stderr,
				"%s: failed to log caller-specified allocation\n",
				__func__);
			exit(-1);
		}
		rc = tagfs_file_map_create(fullpath, fd, fsize, num_extents,
					   ext_list, TAGFS_REG);
	} else {
		rc = tagfs_file_alloc(fd, fullpath, O_RDWR, uid, gid, fsize);
		if (rc) {
			fprintf(stderr, "%s: tagfs_file_alloc(%s, size=%ld) failed\n",
				__func__, fullpath, fsize);
		}
	}
	if (randomize) {
		struct stat st;
		void *addr;
		char *buf;

		rc = stat(fullpath, &st);
		if (rc) {
			fprintf(stderr, "%s: failed to stat newly craeated file %s\n",
				__func__, fullpath);
			exit(-1);
		}
		if (st.st_size != fsize) {
			fprintf(stderr, "%s: file size mismatch %ld/%ld\n",
				__func__, fsize, st.st_size);
		}
		addr = mmap_whole_file(fullpath, 0, NULL);
		if (!addr) {
			fprintf(stderr,"%s: randomize mmap failed\n", __func__);
			exit(-1);
		}
		buf = (char *)addr;

		if (!seed)
			printf("Randomizing buffer with random seed\n");
		randomize_buffer(buf, fsize, seed);
	}

	close(fd);
	return 0;
}

/********************************************************************/
void
tagfs_verify_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Verify the contents of a file:\n"
	       "    %s -S <seed> -f <filename>\n"
	       "\n", progname);
}

int
do_tagfs_cli_verify(int argc, char *argv[])
{
	int c, fd;
	char *filename = NULL;

	size_t fsize = 0;
	int arg_ct = 0;
	s64 seed = 0;
	void *addr;
	char *buf;
	int rc = 0;

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
		/* These options set a */
		{"seed",        required_argument,             0,  'S'},
		{"filename",    required_argument,             0,  'f'},
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no args\n");
		tagfs_verify_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left */
	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no files\n");
		tagfs_verify_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+f:S:h?",
				map_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'S':
			seed = strtoull(optarg, 0, 0);
			break;

		case 'f': {
			filename = optarg;
			printf("filename: %s\n", filename);
			/* TODO: make sure filename is in a tagfs file system */
			break;
		}
		case 'h':
		case '?':
			tagfs_verify_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		exit(-1);
	}
	if (!seed) {
		fprintf(stderr, "Must specify random seed to verify file data\n");
		exit(-1);
	}
	fd = open(filename, O_RDWR, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "open %s failed; rc %d errno %d\n", filename, rc, errno);
		exit(-1);
	}

	addr = mmap_whole_file(filename, 0, &fsize);
	if (!addr) {
		fprintf(stderr,"%s: randomize mmap failed\n", __func__);
		exit(-1);
	}
	buf = (char *)addr;
	rc = validate_random_buffer(buf, fsize, seed);
	if (rc == -1) {
		printf("Success: verified %ld bytes in file %s\n", fsize, filename);
	} else {
		fprintf(stderr, "Verify fail at offset %d of %ld bytes\n", rc, fsize);
		exit(-1);
	}

	return 0;
}

/********************************************************************/


struct tagfs_cli_cmd {
	char *cmd;
	int (*run)(int argc, char **argv);
	void (*help)(int argc, char **argv);
};

static void do_tagfs_cli_help(int argc, char **argv);

#if 0
static int tagfs_cli_help(int argc, char **argv)
{
	do_tagfs_cli_help(argc, argv);
	return 0;
}
#endif

struct
tagfs_cli_cmd tagfs_cli_cmds[] = {

	{"creat",   do_tagfs_cli_creat,   tagfs_creat_usage},
	{"verify",  do_tagfs_cli_verify,  tagfs_verify_usage},
	{"getmap",  do_tagfs_cli_getmap,  tagfs_getmap_usage},
	{"cp",      do_tagfs_cli_cp,      tagfs_cp_usage},
	{"fsck",    do_tagfs_cli_fsck,    tagfs_fsck_usage},
	{"mkmeta",  do_tagfs_cli_mkmeta,  tagfs_mkmeta_usage},
	{"logplay", do_tagfs_cli_logplay, tagfs_logplay_usage},

	{NULL, NULL, NULL}
};

static void
do_tagfs_cli_help(int argc, char **argv)
{
	int i;
	char *progname = xbasename(argv[0]);
	/* Is there a command after "help" on the command line? */
	if (optind < argc) {
		for (i = 0; (tagfs_cli_cmds[i].cmd); i++) {
			if (!strcmp(argv[optind], tagfs_cli_cmds[i].cmd)) {
				tagfs_cli_cmds[i].help(argc, argv);
				return;
			}
		}
	}

	printf("%s: perform operations on a mounted tagfs file system"
	       "for specific files or devices\n"
	       "%s [global_args] <command> [args]\n\n",
	       progname, progname);
	print_global_opts();
	printf("Commands:\n");
	for (i = 0; (tagfs_cli_cmds[i].cmd); i++)
		printf("\t%s\n", tagfs_cli_cmds[i].cmd);
}

int
main(int argc, char **argv)
{
	int c, i;

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers */
	while ((c = getopt_long(argc, argv, "+nh?d:",
				global_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'n':
			dry_run++;
			break;

		case 'h':
		case '?':
			do_tagfs_cli_help(argc, argv);
			return 0;

		default:
			return -1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli: missing command\n\n");
		do_tagfs_cli_help(argc, argv);
		return -1;
	}

	for (i = 0; (tagfs_cli_cmds[i].cmd); i++) {
		if (!strcmp(argv[optind], tagfs_cli_cmds[i].cmd)) {
			optind++; /* move past cmd on cmdline */
			return tagfs_cli_cmds[i].run(argc, argv);
		}
	}

	fprintf(stderr, "%s: Unrecognized command %s\n", argv[0], optarg);
	do_tagfs_cli_help(argc, argv);

	return 0;
}

