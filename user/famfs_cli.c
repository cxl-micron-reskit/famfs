// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/ioctl.h>
#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/user.h>
#include <sys/param.h> /* MIN()/MAX() */
#include <libgen.h>

#include <linux/types.h>
#include <linux/ioctl.h>

typedef __u64 u64;

#include "famfs_ioctl.h"
#include "famfs_lib.h"
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

struct option global_options[] = {
	/* These options set a flag. */
	/* These options don't set a flag.
	 * We distinguish them by their indices.
	 */
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
famfs_logplay_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Play the log into a famfs file system\n"
	       "    %s [Options] <fspath>\n"
	       "\n"
	       "<fspath> must be the mount point or a path that falls within a famfs file system\n"
	       "\n"
	       "Options:\n"
	       "  --read|-r  Get the log via posix read\n"
	       "  --mmap|-m  - Get the log via mmap\n"
	       "  --client|-c - force \"client mode\" (all files read-only)\n"
	       "\n"
	       "\n",
	       progname);
}

/* TODO: add recursive copy? */
int
do_famfs_cli_logplay(int argc, char *argv[])
{
	int c;
	int rc;
	int arg_ct = 0;
	struct famfs_log *logp;
	char mpt_out[PATH_MAX];
	size_t log_size;
	char *fspath;
	int lfd;
	int dry_run = 0;
	int resid = 0;
	int total = 0;
	char *buf = NULL;
	int use_mmap = 0;
	int use_read = 0;
	int client_mode = 0;
	
	/* XXX can't use any of the same strings as the global args! */
	struct option logplay_options[] = {
		/* These options set a */
		{"dryrun",    required_argument,       0,  'n'},
		{"mmap",      no_argument,             0,  'm'},
		{"read",      no_argument,             0,  'r'},
		{"client",    no_argument,             0,  'c'},
		{0, 0, 0, 0}
	};

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no files\n");
		famfs_logplay_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+cmnh?",
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
			famfs_logplay_usage(argc, argv);
			return 0;
		case 'm':
			use_mmap++;
			break;
		case 'r':
			use_read++;
			break;
		case 'c':
			client_mode++;
			printf("client mode\n");
			break;
		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	if (use_mmap && use_read) {
		fprintf(stderr, "The --mmap and --read arguments are mutually exclusive\n");
		famfs_logplay_usage(argc, argv);
		return -1;
	}
	else if (! (use_mmap || use_read)) {
		/* If neither method was explicitly requested, default to mmap */
		use_mmap ++;
	}
	if (optind >= argc) {
		fprintf(stderr, "Must specify at least path "
			"(which must fall within a mounted famfs file system)\n");
		return -1;
	}
	fspath = argv[optind++];

	lfd = open_log_file_read_only(fspath, &log_size, mpt_out);
	if (lfd < 0) {
		fprintf(stderr, "%s: failed to open log file for filesystem %s\n",
			__func__, fspath);
		return -1;
	}

	if (use_mmap) {
		logp = mmap(0, FAMFS_LOG_LEN, PROT_READ, MAP_PRIVATE, lfd, 0);
		if (logp == MAP_FAILED) {
			fprintf(stderr, "%s: failed to mmap log file %s/.meta/log\n",
				__func__, mpt_out);
			close(lfd);
			return -1;
		}
	} else {
		/* Get log via posix read */
		logp = malloc(log_size);
		if (!logp) {
			close(lfd);
			fprintf(stderr, "%s: malloc %ld failed for log\n", __func__, log_size);
			return -ENOMEM;
		}
		resid = log_size;
		buf = (char *)logp;
		do {
			rc = read(lfd, &buf[total], resid);
			if (rc < 0) {
				fprintf(stderr, "%s: error %d reading log file\n",
					__func__, errno);
				return -errno;
			}
			printf("%s: read %d bytes of log\n", __func__, rc);
			resid -= rc;
			total += rc;
		} while (resid > 0);
	}
	famfs_logplay(logp, mpt_out, dry_run, client_mode);
	if (use_mmap)
		munmap(logp, FAMFS_LOG_LEN);
	close(lfd);
	return 0;
}

/********************************************************************/

void
famfs_mkmeta_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Expose the meta files of a famfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_famfs_cli_mkmeta(int argc, char *argv[])
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
		fprintf(stderr, "famfs_cli map: no args\n");
		famfs_mkmeta_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no files\n");
		famfs_mkmeta_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
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
			famfs_mkmeta_usage(argc, argv);
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
		free(realdaxdev);
		return -1;
	}
	famfs_mkmeta(realdaxdev);
	free(realdaxdev);
	return 0;
}

/********************************************************************/

void
famfs_fsck_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Check a famfs file system\n"
	       "    %s <memdevice>\n"
	       "\n", progname);
}

/* TODO: add recursive copy? */
int
do_famfs_cli_fsck(int argc, char *argv[])
{
	int c;

	int arg_ct = 0;
	char *daxdev = NULL;
	int use_mmap = 0;
	int human = 0; /* -h is no longer --help... */
	int verbose = 0;

	/* XXX can't use any of the same strings as the global args! */
	struct option fsck_options[] = {
		/* These options set a */
		{"mmap",        no_argument,             0,  'm'},
		{"human",       no_argument,             0,  'h'},
		{"verbose",     no_argument,             0,  'v'},
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no args\n");
		famfs_fsck_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no files\n");
		famfs_fsck_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?m",
				fsck_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {
		case 'm':
			use_mmap = 1;
			break;
		case 'h':
			human = 1;
			break;
		case 'v':
			verbose++;
			break;
		case '?':
			famfs_fsck_usage(argc, argv);
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
	return famfs_fsck(daxdev, use_mmap, human, verbose);
}


/********************************************************************/

void
famfs_cp_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Copy a file into a famfs file system\n"
	       "    %s cp <srcfile> <destfile>\n"
	       "\n"
	       "NOTE: you need this tool to copy a file into a famfs file system,\n"
	       "but the standard \'cp\' can be used to copy FROM a famfs file system.\n",
	       progname);
}

/* TODO: add recursive copy? */
int
do_famfs_cli_cp(int argc, char *argv[])
{
	int c, rc;
	int arg_ct = 0;
	char *srcfile;
	char *destfile;
	int verbose = 0;

	/* XXX can't use any of the same strings as the global args! */
	struct option cp_options[] = {
		/* These options set a */
		{"filename",    required_argument,             0,  'f'},
		{"verbose",     no_argument,                   0,  'v'},
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no args\n");
		famfs_cp_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no files\n");
		famfs_cp_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?",
				cp_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {
		case 'v':
			verbose++;
			break;
		case 'h':
		case '?':
			famfs_cp_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	srcfile = argv[optind++];
	destfile = argv[optind++];

	rc = famfs_cp(srcfile, destfile, verbose);
	printf("famfs_cp returned %d\n", rc);
	return 0;
}


/********************************************************************/

void
famfs_getmap_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Get the allocation map of a file:\n"
	       "    %s <filename>\n"
	       "\n", progname);
}

int
do_famfs_cli_getmap(int argc, char *argv[])
{
	struct famfs_ioc_map filemap;
	struct famfs_extent *ext_list;
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
		fprintf(stderr, "famfs_cli map: no args\n");
		famfs_getmap_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no files\n");
		famfs_getmap_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
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
			famfs_getmap_usage(argc, argv);
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
	rc = ioctl(fd, FAMFSIOC_MAP_GET, &filemap);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		return rc;
	}
	ext_list = calloc(filemap.ext_list_count, sizeof(struct famfs_extent));
	rc = ioctl(fd, FAMFSIOC_MAP_GETEXT, ext_list);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		return rc;
	}

	printf("File:     %s\n",    filename);
	printf("\tsize:   %ld\n",  filemap.file_size);
	printf("\textents: %ld\n", filemap.ext_list_count);

	for (i = 0; i < filemap.ext_list_count; i++)
		printf("\t\t%llx\t%lld\n", ext_list[i].offset, ext_list[i].len);

	close(rc);

	return 0;
}

/********************************************************************/

void
famfs_clone_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Clone a file, creating a second file with the same extent list:\n"
	       "    %s <src_file> <dest_file>\n"
	       "\nNOTE: this creates a file system error and is for tesstsing only!!\n"
	       "\n", progname);
}

int
do_famfs_cli_clone(int argc, char *argv[])
{
	int c;
	int arg_ct = 0;
	int verbose = 0;

	char *srcfile = NULL;
	char *destfile = NULL;
	char srcfullpath[PATH_MAX];

	/* XXX can't use any of the same strings as the global args! */
	struct option cp_options[] = {
		/* These options set a */
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no args\n");
		famfs_clone_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no files\n");
		famfs_clone_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?",
				cp_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'v':
			verbose++;
			break;
		case 'h':
		case '?':
			famfs_clone_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	/* There should be 2 more arguments */
	if (optind > (argc - 1)) {
		fprintf(stderr, "%s: too few arguents\n", __func__);
		famfs_clone_usage(argc, argv);
		return -1;
	}
	srcfile  = argv[optind++];
	destfile = argv[optind++];
	if (realpath(srcfile, srcfullpath) == NULL) {
		fprintf(stderr, "%s: bad source path %s\n", __func__, srcfile);
		return -1;
	}

	return famfs_clone(srcfile, destfile, verbose);
}

/********************************************************************/

void
famfs_creat_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Create a file backed by free space:\n"
	       "    %s -s <size> <filename>\n\n"
	       "\nCreate a file containing randomized data from a specific seed:\n"
	       "    %s -s size --randomize --seed <myseed> <filename>"
	       "Create a file backed by free space, with octal mode 0644:\n"
	       "    %s -s <size> -m 0644 <filename>\n\n"
	       "Options:\n"
	       "--size|-s <size>[kKmMgG]   - Required file size\n"
	       "--seed|-S <random-seed>    - Optional seed for randomization\n"
	       "--randomize|-r             - Optional - will randomize with provided seed\n"
	       "--mode|-m <octal-mode>     - Default is 0644\n"
	       "--uid|-u <int uid>         - Default is caller's uid\n"
	       "--gid|-g <int gid>         - Default is caller's gid\n"
	       "\n",
	       progname, progname, progname);
}

static s64 get_multiplier(const char *endptr)
{
	size_t multiplier = 1;

	if (!endptr)
		return 1;

	switch (*endptr) {
	case 'k':
	case 'K':
		multiplier = 1024;
		break;
	case 'm':
	case 'M':
		multiplier = 1024 * 1024;
		break;
	case 'g':
	case 'G':
		multiplier = 1024 * 1024 * 1024;
		break;
	case 0:
		return 1;
	}
	++endptr;
	if (*endptr) /* If the unit was not the last char in string, it's an error */
		return -1;
	return multiplier;
}

int
do_famfs_cli_creat(int argc, char *argv[])
{
	int c, rc, fd;
	char *filename = NULL;
	char fullpath[PATH_MAX];

	size_t fsize = 0;
	s64 mult;
	int arg_ct = 0;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	mode_t mode = 0644;
	s64 seed;
	int randomize = 0;
	int verbose = 0;

	/* TODO: allow passing in uid/gid/mode on command line*/

	/* XXX can't use any of the same strings as the global args! */
	struct option creat_options[] = {
		/* These options set a flag. */
		{"size",        required_argument,             0,  's'},
		{"seed",        required_argument,             0,  'S'},
		{"randomize",   no_argument,                   0,  'r'},
		{"mode",        required_argument,             0,  'm'},
		{"uid",         required_argument,             0,  'u'},
		{"gid",         required_argument,             0,  'g'},
		{"verbose",     no_argument,                   0,  'v'},
		/* These options don't set a flag.
		 * We distinguish them by their indices.
		 */
		/*{"dryrun",       no_argument,       0, 'n'}, */
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "famfs_cli creat: no args\n");
		famfs_creat_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli creat: no files\n");
		famfs_creat_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+s:S:m:u:g:rh?v",
				creat_options, &optind)) != EOF) {
		char *endptr;

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 's':
			fsize = strtoull(optarg, &endptr, 0);
			if (fsize <= 0) {
				fprintf(stderr, "invalid file size %ld\n",
					fsize);
				exit(-1);
			}
			mult = get_multiplier(endptr);
			if (mult > 0)
				fsize *= mult;
			break;

		case 'S':
			seed = strtoull(optarg, 0, 0);
			break;

		case 'm':
			mode = strtol(optarg, 0, 8); /* Must be valid octal */
			break;

		case 'u':
			uid = strtol(optarg, 0, 0);
			if (uid < 0) {
				fprintf(stderr, "uid must be positive integer\n");
				exit(-1);
			}
			break;

		case 'g':
			gid = strtol(optarg, 0, 0);
			if (gid < 0) {
				fprintf(stderr, "gid must be positive integer\n");
				exit(-1);
			}
			break;

		case 'r':
			randomize++;
			break;
		case 'v':
			verbose++;
			break;

		case 'h':
		case '?':
			famfs_creat_usage(argc, argv);
			return 0;

		default:
			printf("%s: urecognized argument (%c)\n", __func__, c);
			return -1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Must specify at least one dax device\n");
		return -1;
	}
	filename = argv[optind++];
	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		exit(-1);
	}

	if (!fsize) {
		fprintf(stderr, "Non-zero file size is required\n");
		exit(-1);
	}

	printf("mode: %o\n", mode);
	fd = famfs_mkfile(filename, mode, uid, gid, fsize, verbose);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, fullpath);
		exit(-1);
	}
	if (randomize) {
		struct stat st;
		void *addr;
		char *buf;

		rc = fstat(fd, &st);
		if (rc) {
			fprintf(stderr, "%s: failed to stat newly craeated file %s\n",
				__func__, fullpath);
			exit(-1);
		}
		if (st.st_size != fsize) {
			fprintf(stderr, "%s: file size mismatch %ld/%ld\n",
				__func__, fsize, st.st_size);
		}
		addr = mmap(0, fsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (!addr) {
			fprintf(stderr, "%s: randomize mmap failed\n", __func__);
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
famfs_mkdir_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Create a directory in a famfs file system:\n"
	       "    %s <dirname>\n\n"
	       "\n"
	       "(the mkdir will be logged\n\n",
	       progname);
}

int
do_famfs_cli_mkdir(int argc, char *argv[])
{
	int c;

	mode_t mode = S_IRUSR|S_IWUSR;
	char *dirpath   = NULL;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	int arg_ct = 0;

	/* TODO: allow passing in uid/gid/mode on command line*/

	/* XXX can't use any of the same strings as the global args! */
	struct option mkdir_options[] = {
		/* These options set a flag. */

		/* These options don't set a flag.
		 * We distinguish them by their indices.
		 */
		/*{"dryrun",       no_argument,       0, 'n'}, */
		{0, 0, 0, 0}
	};

	if (optind >= argc) {
		fprintf(stderr, "famfs_cli mkdir: no args\n");
		famfs_mkdir_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli mkdir: no files\n");
		famfs_mkdir_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?",
				mkdir_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			famfs_mkdir_usage(argc, argv);
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

	dirpath  = argv[optind++];

	return famfs_mkdir(dirpath, mode, uid, gid);
}

/********************************************************************/
void
famfs_verify_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "Verify the contents of a file:\n"
	       "    %s -S <seed> -f <filename>\n"
	       "\n", progname);
}

int
do_famfs_cli_verify(int argc, char *argv[])
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
		fprintf(stderr, "famfs_cli map: no args\n");
		famfs_verify_usage(argc, argv);
		return -1;
	}

	/* The next stuff on the command line is file names;
	 * err if nothing is left
	 */
	if (optind >= argc) {
		fprintf(stderr, "famfs_cli map: no files\n");
		famfs_verify_usage(argc, argv);
		return -1;
	}
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
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
			/* TODO: make sure filename is in a famfs file system */
			break;
		}
		case 'h':
		case '?':
			famfs_verify_usage(argc, argv);
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
		fprintf(stderr, "%s: randomize mmap failed\n", __func__);
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


struct famfs_cli_cmd {
	char *cmd;
	int (*run)(int argc, char **argv);
	void (*help)(int argc, char **argv);
};

static void do_famfs_cli_help(int argc, char **argv);

struct
famfs_cli_cmd famfs_cli_cmds[] = {

	{"creat",   do_famfs_cli_creat,   famfs_creat_usage},
	{"mkdir",   do_famfs_cli_mkdir,   famfs_mkdir_usage},
	{"verify",  do_famfs_cli_verify,  famfs_verify_usage},
	{"getmap",  do_famfs_cli_getmap,  famfs_getmap_usage},
	{"clone",   do_famfs_cli_clone,   famfs_clone_usage},
	{"cp",      do_famfs_cli_cp,      famfs_cp_usage},
	{"fsck",    do_famfs_cli_fsck,    famfs_fsck_usage},
	{"mkmeta",  do_famfs_cli_mkmeta,  famfs_mkmeta_usage},
	{"logplay", do_famfs_cli_logplay, famfs_logplay_usage},

	{NULL, NULL, NULL}
};

static void
do_famfs_cli_help(int argc, char **argv)
{
	int i;
	char *progname = xbasename(argv[0]);
	/* Is there a command after "help" on the command line? */
	if (optind < argc) {
		for (i = 0; (famfs_cli_cmds[i].cmd); i++) {
			if (!strcmp(argv[optind], famfs_cli_cmds[i].cmd)) {
				famfs_cli_cmds[i].help(argc, argv);
				return;
			}
		}
	}

	printf("%s: perform operations on a mounted famfs file system "
	       "for specific files or devices\n"
	       "%s [global_args] <command> [args]\n\n",
	       progname, progname);
	print_global_opts();
	printf("Commands:\n");
	for (i = 0; (famfs_cli_cmds[i].cmd); i++)
		printf("\t%s\n", famfs_cli_cmds[i].cmd);
}

int
main(int argc, char **argv)
{
	int c, i;

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?:",
				global_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'h':
		case '?':
			do_famfs_cli_help(argc, argv);
			return 0;

		default:
			return -1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "famfs_cli: missing command\n\n");
		do_famfs_cli_help(argc, argv);
		return -1;
	}

	for (i = 0; (famfs_cli_cmds[i].cmd); i++) {
		if (!strcmp(argv[optind], famfs_cli_cmds[i].cmd)) {
			optind++; /* move past cmd on cmdline */
			return famfs_cli_cmds[i].run(argc, argv);
		}
	}

	fprintf(stderr, "%s: Unrecognized command %s\n", argv[0], optarg);
	do_famfs_cli_help(argc, argv);

	return 0;
}

