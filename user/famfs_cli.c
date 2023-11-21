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

int verbose_flag;  /* JG: ignored at the moment */
static int dry_run;

struct option global_options[] = {
	/* These options set a flag. */
	{"verbose",          no_argument, &verbose_flag,  1 },
	{"brief",            no_argument, &verbose_flag,  0 },
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
	       "    %s <memdevice>\n"
	       "\n", progname);
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
	char *filename;
	int lfd;
	int dry_run = 0;
	int resid = 0;
	int total = 0;
	char *buf = NULL;

/* XXX can't use any of the same strings as the global args! */
	struct option logplay_options[] = {
		/* These options set a */
		{"dryrun",      required_argument,             0,  'n'},
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
			famfs_logplay_usage(argc, argv);
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

	lfd = open_log_file_read_only(filename, &log_size, mpt_out);
	if (lfd < 0) {
		fprintf(stderr, "%s: failed to open log file for filesystem %s\n",
			__func__, filename);
		return -1;
	}

	logp = malloc(log_size);
	if (!logp) {
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

	close(lfd);
	famfs_logplay(logp, mpt_out, dry_run);
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

	/* XXX can't use any of the same strings as the global args! */
	struct option fsck_options[] = {
		/* These options set a */
		{"daxdev",      required_argument,             0,  'D'},
		{"fsdaxdev",    required_argument,             0,  'F'},
		{"mmap",        required_argument,             0,  'm'},
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
	while ((c = getopt_long(argc, argv, "+h?m",
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
	return famfs_fsck(daxdev, use_mmap, verbose_flag);
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

	/* XXX can't use any of the same strings as the global args! */
	struct option cp_options[] = {
		/* These options set a */
		{"filename",    required_argument,             0,  'f'},
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
			famfs_cp_usage(argc, argv);
			return 0;

		default:
			printf("default (%c)\n", c);
			return -1;
		}
	}

	srcfile = argv[optind++];
	destfile = argv[optind++];

	rc = famfs_cp(srcfile, destfile);
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
	struct famfs_ioc_map filemap;
	struct famfs_extent *ext_list;
	int c;
	int rc = 0;
	int arg_ct = 0;

	char *srcfile = NULL;
	char *destfile = NULL;
	char srcfullpath[PATH_MAX];
	char destfullpath[PATH_MAX];
	int lfd = 0;
	int sfd = 0;
	int dfd = 0;
	char mpt_out[PATH_MAX];
	char *relpath;
	struct famfs_log *logp;
	void *addr;
	size_t log_size;
	struct famfs_simple_extent *se;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	mode_t mode = S_IRUSR|S_IWUSR;

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

	/*
	 * Open source file and make sure it's a famfs file
	 */
	sfd = open(srcfile, O_RDONLY, 0);
	if (sfd < 0) {
		fprintf(stderr, "%s: failed to open source file %s\n",
			__func__, srcfile);
		return -1;
	}
	if (__file_not_famfs(sfd)) {
		fprintf(stderr, "%s: source file %s is not a famfs file\n",
			__func__, srcfile);
		return -1;
	}

	/*
	 * Get map for source file
	 */
	rc = ioctl(sfd, FAMFSIOC_MAP_GET, &filemap);
	if (rc) {
		fprintf(stderr, "%s: MAP_GET returned %d errno %d\n", __func__, rc, errno);
		goto err_out;
	}
	ext_list = calloc(filemap.ext_list_count, sizeof(struct famfs_extent));
	rc = ioctl(sfd, FAMFSIOC_MAP_GETEXT, ext_list);
	if (rc) {
		fprintf(stderr, "%s: GETEXT returned %d errno %d\n", __func__, rc, errno);
		goto err_out;
	}

	/*
	 * For this operation we need to open the log file, which also gets us
	 * the mount point path
	 */
	lfd = open_log_file_writable(srcfullpath, &log_size, mpt_out);
	addr = mmap(0, log_size, PROT_READ | PROT_WRITE, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to mmap log file\n", __func__);
		rc = -1;
		goto err_out;
	}
	close(lfd);
	lfd = 0;
	logp = (struct famfs_log *)addr;

	/* Create the destination file. This will be unlinked later if we don't get all
	 * the way through the operation.
	 */
	dfd = famfs_file_create(destfile, mode, uid, gid);
	if (dfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, destfile);
		rc = -1;
		goto err_out;
	}

	/*
	 * Create the file before logging, so we can avoid a BS log entry if the
	 * kernel rejects the caller-supplied allocation ext list
	 */
	/* Ugh need to unify extent types... XXX */
	se = famfs_ext_to_simple_ext(ext_list, filemap.ext_list_count);
	if (!se) {
		rc = -ENOMEM;
		goto err_out;
	}
	rc = famfs_file_map_create(destfile, dfd, filemap.file_size, filemap.ext_list_count,
				   se, FAMFS_REG);
	if (rc) {
		fprintf(stderr, "%s: failed to create destination file\n", __func__);
		exit(-1);
	}

	/* Now have created the destionation file (and therefore we know it is in a famfs
	 * mount, we need its relative path of
	 */
	if (realpath(destfile, destfullpath) == NULL) {
		close(dfd);
		unlink(destfullpath);
		return -1;
	}
	relpath = famfs_relpath_from_fullpath(mpt_out, destfullpath);
	if (!relpath) {
		rc = -1;
		unlink(destfullpath);
		goto err_out;
	}

	/* XXX - famfs_log_file_creation should only be called outside
	 * famfs_lib.c if we are intentionally doing extent list allocation
	 * bypassing famfs_lib. This is useful for testing, by generating
	 * problematic extent lists on purpoose...
	 */
	rc = famfs_log_file_creation(logp, filemap.ext_list_count, se,
				     relpath, O_RDWR, uid, gid, filemap.file_size);
	if (rc) {
		fprintf(stderr,
			"%s: failed to log caller-specified allocation\n",
			__func__);
		rc = -1;
		unlink(destfullpath);
		goto err_out;
	}
	/***************/

	close(rc);

	return 0;
err_out:
	if (lfd > 0)
		close(lfd);
	if (sfd > 0)
		close(sfd);
	if (lfd > 0)
		close(lfd);
	if (dfd > 0)
		close(dfd);
	return rc;
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
	       "--size|-s <size>           - Required file size\n"
	       "--seed|-S <random-seed>    - Optional seed for randomization\n"
	       "--randomize|-r             - Optional - will randomize with provided seed\n"
	       "--mode|-m <octal-mode>     - Default is 0644\n"
	       "--uid|-u <int uid>         - Default is caller's uid\n"
	       "--gid|-g <int gid>         - Default is caller's gid\n"
	       "\n",
	       progname, progname, progname);
}

int
do_famfs_cli_creat(int argc, char *argv[])
{
	int c, rc, fd;
	char *filename = NULL;
	char fullpath[PATH_MAX];

	size_t fsize = 0;
	int arg_ct = 0;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	mode_t mode = 0644;
	s64 seed;
	int randomize = 0;

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
	while ((c = getopt_long(argc, argv, "+s:S:m:u:g:rh?",
				creat_options, &optind)) != EOF) {
		/* printf("optind:argv = %d:%s\n", optind, argv[optind]); */

		/* Detect the end of the options. */
		if (c == -1)
			break;

		arg_ct++;
		switch (c) {

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
	fd = famfs_mkfile(filename, mode, uid, gid, fsize);
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

