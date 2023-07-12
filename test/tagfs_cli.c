
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

#include "../tagfs/tagfs_ioctl.h"
#include "tagfs_lib.h"

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
	unsigned char *progname = argv[0];

	printf("\n"
	       "Play the log into a tagfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_logplay(int argc, char *argv[])
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
	unsigned char *realdaxdev = NULL;

	char *srcfile;
	char *destfile;

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
		/* These options set a */
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "tagfs_cli map: no args\n");
		tagfs_logplay_usage(argc, argv);
		return -1;
	}

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
	while ((c = getopt_long(argc, argv, "+h?",
				global_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

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
	/* TODO: multiple devices? */
	daxdev = argv[optind++];
	realdaxdev = realpath(daxdev, NULL);
	if (!realdaxdev) {
		fprintf(stderr, "%s: realpath(%s) returned %d\n", __func__, errno);
		return -1;
	}
	//tagfs_logplay(realdaxdev);
	return 0;
}

/********************************************************************/

void
tagfs_mkmeta_usage(int   argc,
	    char *argv[])
{
	unsigned char *progname = argv[0];

	printf("\n"
	       "Check a tagfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_mkmeta(int argc, char *argv[])
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
	unsigned char *realdaxdev = NULL;

	char *srcfile;
	char *destfile;

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
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
				global_options, &optind)) != EOF) {
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
		fprintf(stderr, "%s: realpath(%s) returned %d\n", __func__, errno);
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
	unsigned char *progname = argv[0];

	printf("\n"
	       "Check a tagfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_fsck(int argc, char *argv[])
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

	char *srcfile;
	char *destfile;

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
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
				global_options, &optind)) != EOF) {
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
	return tagfs_fsck(daxdev, 1 /* verbose */);
}


/********************************************************************/

void
tagfs_cp_usage(int   argc,
	    char *argv[])
{
	unsigned char *progname = argv[0];

	printf("\n"
	       "Copy a file into a tagfs file system\n"
	       "    %s <srcfile> <destfile>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_tagfs_cli_cp(int argc, char *argv[])
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

	char *srcfile;
	char *destfile;

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
		/* These options set a */
		{"daxdev",      required_argument,             0,  'D'},
		{"fsdaxdev",    required_argument,             0,  'F'},
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
				global_options, &optind)) != EOF) {
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
	unsigned char *progname = argv[0];

	printf("\n"
	       "Mape one or more HPA based extent:\n"
	       "    %s -n <num_extents> -o <hpa> -l <len> [-o <hpa> -l <len> ... ] <filename>\n"
	       "\n", progname);
	printf(
	       "Mape one or more dax-based extents:"
	       "    %s --daxdev <daxdev> -n <num_extents> -o <offset> -l <len> [-o <offset> -l <len> ... ] <filename>\n"
	       "\n", progname);
}

int
do_tagfs_cli_getmap(int argc, char *argv[])
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

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
		/* These options set a */
		{"filename",    required_argument,             0,  'f'},
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
	while ((c = getopt_long(argc, argv, "+f:h?",
				global_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'f': {
			filename = optarg;
			printf("filename: %s\n", filename);
			/* TODO: make sure filename is in a tagfs file system */
			break;
		}
		case 'h':
		case '?':
			tagfs_getmap_usage(argc, argv);
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
	fd = open(filename, O_RDWR | O_CREAT, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "open/mape failed; rc %d errno %d\n", rc, errno);
		exit(-1);
	}
	rc = ioctl(fd, TAGFSIOC_MAP_GET, &filemap);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		return rc;
	}
	ext_list = calloc(filemap.ext_list_count, sizeof(struct tagfs_extent));
	rc = ioctl(fd, TAGFSIOC_MAP_GETEXT, &ext_list);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		return rc;
	}

	printf("File:     %s\n",    filename);
	printf("\tsize:   %lld\n",  filemap.file_size);
	printf("\textents: %lld\n", filemap.ext_list_count);

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

int
do_tagfs_cli_creat(int argc, char *argv[])
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

	/* XXX can't use any of the same strings as the global args! */
	struct option creat_options[] = {
		/* These options set a flag. */
		{"address",     required_argument, &verbose_flag,  'o' },
		{"length",      required_argument, &verbose_flag,  'l'},
		{"num_extents", required_argument,             0,  'n'},
		{"filename",    required_argument,             0,  'f'},
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
	while ((c = getopt_long(argc, argv, "+n:o:l:f:h?",
				global_options, &optind)) != EOF) {
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
				filemap.ext_list = ext_list;
				filemap.ext_list_count = num_extents;
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
			tagfs_creat_usage(argc, argv);
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

	return 0;
}

/********************************************************************/


struct tagfs_cli_cmd {
	char *cmd;
	int (*run)(int argc, char **argv);
	void (*help)(int argc, char **argv);
};

static void do_tagfs_cli_help(int argc, char **argv);

static int tagfs_cli_help(int argc, char **argv)
{
	do_tagfs_cli_help(argc, argv);
	return 0;
}

struct
tagfs_cli_cmd tagfs_cli_cmds[] = {

	{"creat",   do_tagfs_cli_creat,   tagfs_creat_usage},
	{"getmap",  do_tagfs_cli_getmap,  tagfs_getmap_usage},
	{"cp",      do_tagfs_cli_cp,      tagfs_cp_usage},
	{"fsck",    do_tagfs_cli_fsck,    tagfs_fsck_usage},
	{"mkmeta",  do_tagfs_cli_mkmeta,  tagfs_mkmeta_usage},
	{"logplay", do_tagfs_cli_logplay, tagfs_logplay_usage},

#if 0
	{"snoop",   do_tagfs_cli_snoop,   help_tagfs_cli_snoop },
	{"pagemap", do_tagfs_cli_pagemap, help_tagfs_cli_pagemap },
	{"cread",   do_tagfs_cli_cread,   help_tagfs_cli_cread },
	{"put",     do_tagfs_cli_put,     help_tagfs_cli_put },
	{"mmdebug", do_tagfs_cli_mmdebug, help_tagfs_cli_mmdebug },
	{"snorg",   do_tagfs_cli_snorg,   help_tagfs_cli_snorg },
	{"foo",     do_tagfs_cli_foo,     help_tagfs_cli_foo   },
	{"help",    tagfs_cli_help,       do_tagfs_cli_help },
#endif
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

