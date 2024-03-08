// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

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
#include <sys/mount.h>

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/famfs_ioctl.h>

#include "famfs_lib.h"
#include "random_buffer.h"
#include "mu_mem.h"

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
	       "famfs logplay: Play the log of a mounted famfs file system\n"
	       "\n"
	       "This administrative command is necessary after mounting a famfs file system\n"
	       "and performing a 'famfs mkmeta' to instantiate all logged files\n"
	       "\n"
	       "    %s logplay [args] <mount_point>\n"
	       "\n"
	       "Arguments:\n"
	       "    -r|--read   - Get the superblock and log via posix read\n"
	       "    -m|--mmap   - Get the log via mmap\n"
	       "    -c|--client - force \"client mode\" (all files read-only)\n"
	       "    -n|--dryrun - Process the log but don't instantiate the files & directories\n"
	       "\n"
	       "\n",
	       progname);
}

int
do_famfs_cli_logplay(int argc, char *argv[])
{
	int c;
	int arg_ct = 0;
	char *fspath;
	int dry_run = 0;
	int use_mmap = 0;
	int use_read = 0;
	int client_mode = 0;
	int verbose = 0;

	/* XXX can't use any of the same strings as the global args! */
	struct option logplay_options[] = {
		/* These options set a */
		{"dryrun",    required_argument,       0,  'n'},
		{"mmap",      no_argument,             0,  'm'},
		{"read",      no_argument,             0,  'r'},
		{"client",    no_argument,             0,  'c'},
		{"verbose",    no_argument,            0,  'v'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vrcmnh?",
				logplay_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {
		case 'n':
			dry_run++;
			printf("Logplay: dry_run selected\n");
			break;
		case 'h':
		case '?':
			famfs_logplay_usage(argc, argv);
			return 0;
		case 'm':
			use_mmap = 1;
			break;
		case 'r':
			fprintf(stderr,
				"%s: warning: the read option can cause cache coherency problems\n",
				__func__);
			use_read = 1;
			break;
		case 'c':
			client_mode++;
			break;
		case 'v':
			verbose++;
			break;
		}
	}

	if (use_mmap && use_read) {
		fprintf(stderr,
			"Error: The --mmap and --read arguments are mutually exclusive\n\n");
		famfs_logplay_usage(argc, argv);
		return -1;
	} else if (!(use_mmap || use_read)) {
		/* If neither method was explicitly requested, default to mmap */
		use_mmap = 1;
	}
	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify mount_point "
			"(actually any path within a famfs file system will work)\n");
		famfs_logplay_usage(argc, argv);
		return -1;
	}
	fspath = argv[optind++];

	return famfs_logplay(fspath, use_mmap, dry_run, client_mode, verbose);
}

/********************************************************************/
void
famfs_mount_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "famfs mount: mount a famfs file system and make it ready to use\n"
	       "\n"
	       "We recommend using the \'famfs mount\' command rather than the native system mount\n"
	       "command, because there are additional steps necessary to make a famfs file system\n"
	       "ready to use after the system mount (see mkmeta and logplay). This command takes\n"
	       "care of the whole job.\n"
	       "\n"
	       "    %s mount <memdevice> <mountpoint>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?             - Print this message\n"
	       "    -R|--remount   - Re-mount\n"
	       "    -v|--verbose   - Print verbose output\n"
	       "\n", progname);
}

int
do_famfs_cli_mount(int argc, char *argv[])
{
	int c;
	int rc;
	int arg_ct = 0;
	int verbose = 0;
	int use_read = 0;
	int use_mmap = 0;
	char *mpt = NULL;
	int remaining_args;
	char *daxdev = NULL;
	char *realmpt = NULL;
	char *realdaxdev = NULL;
	unsigned long mflags = MS_NOATIME | MS_NOSUID | MS_NOEXEC | MS_NODEV;

	struct option mkmeta_options[] = {
		/* These options set a */
		{"remount",    no_argument,            0,  'R'},
		{"read",       no_argument,             0,  'r'},
		{"mmap",       no_argument,             0,  'm'},
		{"verbose",    no_argument,            0,  'v'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?Rrmv",
				mkmeta_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			famfs_mount_usage(argc, argv);
			return 0;
		case 'v':
			verbose++;
			break;
		case 'm':
			use_mmap = 1;
			break;
		case 'r':
			fprintf(stderr,
				"%s: warning: the read option can cause cache coherency problems\n",
				__func__);
			use_read = 1;
			break;
		case 'R':
			mflags |= MS_REMOUNT;
			break;
		}
	}

	if (use_mmap && use_read) {
		fprintf(stderr,
			"Error: The --mmap and --read arguments are mutually exclusive\n\n");
		famfs_logplay_usage(argc, argv);
		return -1;
	} else if (!(use_mmap || use_read)) {
		/* If neither method was explicitly requested, default to mmap */
		use_mmap = 1;
	}
	remaining_args = argc - optind;

	if (remaining_args != 2) {
		fprintf(stderr, "famfs mount error: <daxdev> and <mountpoint> args are required\n");
		famfs_mount_usage(argc, argv);
		return -1;
	}

	daxdev = argv[optind++];
	realdaxdev = realpath(daxdev, NULL);
	if (!realdaxdev) {
		fprintf(stderr, "famfs mount: daxdev (%s) not found\n",  daxdev);
		free(realdaxdev);
		return -1;
	}
	mpt = argv[optind++];
	realmpt = realpath(mpt, NULL);
	if (!realmpt) {
		fprintf(stderr, "famfs mount: mount pt (%s) not found\n", mpt);
		free(realmpt);
		rc = -1;
		goto err_out;
	}
	if (!famfs_module_loaded(1)) {
		fprintf(stderr, "famfs mount: famfs kernel module is not loaded!\n");
		fprintf(stderr, "famfs mount: try 'sudo modprobe famfs'\n");
		rc = -1;
		goto err_out;
	}

	rc = mount(realdaxdev, realmpt, "famfs", mflags, "");
	if (rc) {
		fprintf(stderr, "famfs mount: mount returned %d; errno %d\n", rc, errno);
		perror("mount fail\n");
		return rc;
	}

	rc = famfs_mkmeta(realdaxdev);
	if (rc)
		fprintf(stderr, "famfs mount: ignoring err %d from mkmeta\n", rc);


	rc = famfs_logplay(realmpt, use_mmap, 0, 0, verbose);

err_out:
	free(realdaxdev);
	free(realmpt);
	return rc;
}

/********************************************************************/

void
famfs_mkmeta_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "famfs mkmeta:\n"
	       "\n"
	       "The famfs file system exposes its superblock and log to its userspace components\n"
	       "as files. After telling the linux kernel to mount a famfs file system, you need\n"
	       "to run 'famfs mkmeta' in order to expose the critical metadata, and then run\n"
	       "'famfs logplay' to play the log. Files will not be visible until these steps\n"
	       "have been performed.\n"
	       "\n"
	       "    %s mkmeta <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0\n"
	       "\n"
	       "Arguments:\n"
	       "    -?               - Print this message\n"
	       "    -v|--verbose     - Print verbose output\n"
	       "\n", progname);
}

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

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?",
				mkmeta_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			famfs_mkmeta_usage(argc, argv);
			return 0;
		}
	}

	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify at least one dax device\n");
		famfs_mkmeta_usage(argc, argv);
		return -1;
	}

	daxdev = argv[optind++];
	realdaxdev = realpath(daxdev, NULL);
	if (!realdaxdev) {
		fprintf(stderr, "%s: unable to rationalize daxdev path from (%s) rc %d\n",
			__func__, daxdev, errno);
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
	       "famfs fsck: check a famfs file system\n"
	       "\n"
	       "This command checks the validity of the superblock and log, and scans the\n"
	       "superblock for cross-linked files.\n"
	       "\n"
	       "Check an unmounted famfs file system\n"
	       "    %s fsck [args] <memdevice>  # Example memdevices: /dev/pmem0 or /dev/dax0.0\n"
	       "Check a mounted famfs file system:\n"
	       "    %s [args] <mount point>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?           - Print this message\n"
	       "    -m|--mmap    - Access the superblock and log via mmap\n"
	       "    -h|--human   - Print sizes in a human-friendly form\n"
	       "    -v|--verbose - Print debugging output while executing the command\n"
	       "\n"
	       "Exit codes:\n"
	       "  0  - No errors were found\n"
	       " !=0 - Errors were found\n"
	       "\n", progname, progname);
}

int
do_famfs_cli_fsck(int argc, char *argv[])
{
	int c;

	int arg_ct = 0;
	char *daxdev = NULL;
	int use_mmap = 0;
	int use_read = 0;
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

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?mr",
				fsck_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {
		case 'm':
			use_mmap = 1;
			break;
		case 'r':
			fprintf(stderr,
				"%s: warning: the read option can cause cache coherency problems\n",
				__func__);
			use_read = 1;
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
		}
	}

	if (use_mmap && use_read) {
		fprintf(stderr,
			"Error: The --mmap and --read arguments are mutually exclusive\n\n");
		famfs_fsck_usage(argc, argv);
		return -1;
	} else if (!(use_mmap || use_read)) {
		/* If neither method was explicitly requested, default to mmap */
		use_mmap = 1;
	}
	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify at least one dax device\n");
		famfs_fsck_usage(argc, argv);
		return -1;
	}

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
	       "famfs cp: Copy one or more files and directories into a famfs file system\n"
	       "\n"
	       "Copy a file into a famfs file system\n"
	       "    %s cp [args] <srcfile> <destfile> # destfile must not already exist\n"
	       "\n"
	       "Copy a file into a directory of a famfs file system with the same basename\n"
	       "    %s cp [args] <srcfile> <dirpath>\n"
	       "\n"
	       "Copy a wildcard set of files to a directory\n"
	       "    %s cp [args]/path/to/* <dirpath>\n"
	       "\n"
	       "Arguments\n"
	       "    -h|-?            - Print this message\n"
	       "    -m|--mode=<mode> - Set mode (as in chmod) to octal value\n"
	       "    -u|--uid=<uid>   - Specify uid (default is current user's uid)\n"
	       "    -g|--gid=<gid>   - Specify uid (default is current user's gid)\n"
	       "    -v|verbose       - print debugging output while executing the command\n"
	       "\n"
	       "NOTE 1: 'famfs cp' will never overwrite an existing file, which is a side-effect\n"
	       "        of the facts that famfs never does delete, truncate or allocate-on-write\n"
	       "NOTE 2: you need this tool to copy a file into a famfs file system,\n"
	       "        but the standard \'cp\' can be used to copy FROM a famfs file system.\n"
	       "        If you inadvertently copy files into famfs using the standard 'cp' (or\n"
	       "        other non-famfs tools), the files created will be invalid. Any such files\n"
	       "        can be found using 'famfs check'.\n"
	       "\n",
	       progname, progname, progname);
}

int
do_famfs_cli_cp(int argc, char *argv[])
{
	int c;
	int arg_ct = 0;
	int verbose = 0;
	int remaining_args;
	mode_t mode = 0; /* null mode inherits mode form source file */
	uid_t uid = getuid();
	gid_t gid = getgid();
	mode_t current_umask;
	int recursive = 0;
	int rc;

	/* XXX can't use any of the same strings as the global args! */
	struct option cp_options[] = {
		/* These options set a */
		{"mode",        required_argument,    0,  'm'},
		{"uid",         required_argument,             0,  'u'},
		{"gid",         required_argument,             0,  'g'},
		{"verbose",     no_argument,          0,  'v'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+rm:u:g:vh?",
				cp_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {
		case 'v':
			verbose++;
			break;
		case 'h':
		case '?':
			famfs_cp_usage(argc, argv);
			return 0;
		case 'r':
			recursive = 1;
			break;
		case 'm':
			mode = strtol(optarg, 0, 8); /* Must be valid octal */
			break;

		case 'u':
			uid = strtol(optarg, 0, 0);
			break;

		case 'g':
			gid = strtol(optarg, 0, 0);
			break;
		}
	}

	remaining_args = argc - optind;

	if (remaining_args < 2) {
		fprintf(stderr, "famfs cp error: source and destination args are required\n");
		famfs_cp_usage(argc, argv);
		return -1;
	}

	/* This is horky, but OK for the cli */
	current_umask = umask(0022);
	umask(current_umask);
	mode &= ~(current_umask);

	rc = famfs_cp_multi(argc - optind, &argv[optind], mode, uid, gid, recursive, verbose);
	return rc;
}


/********************************************************************/

void
famfs_check_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "famfs check: check the contents of a famfs file system.\n"
	       "\n"
	       "Unlike fsck, which validates the log and that there are no cross-linked files,\n"
	       "this command examines every file in a mounted famfs instance and checks that\n"
	       "the allocation metadata is valid. To get the full picture you need both\n"
	       "'famfs fsck' and 'famfs check'.\n"
	       "\n"
	       "This is imporant for a couple of reasons. Although creating a valid famfs file\n"
	       "requires use of the famfs cli or api, it is possible to create invalid files with\n"
	       "the standard system tools (cp, etc.). It is also conceivable that a bug in the\n"
	       "famfs api and/or cli would leave an improperly configured file in place after\n"
	       "unsuccessful error recovery. This command will find those invalid\n"
	       "files (if any) and report them.\n"
	       "\n"
	       "    %s check [args] <mount point>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?           - Print this message\n"
	       "    -v|--verbose - Print debugging output while executing the command\n"
	       "                   (the verbose arg can be repeated for more verbose output)\n"
	       "\n"
	       "Exit codes:\n"
	       "   0    - All files properly mapped\n"
	       "When non-zero, the exit code is the bitwise or of the following values:\n"
	       "   1    - At least one unmapped file found\n"
	       "   2    - Superblock file missing or corrupt\n"
	       "   4    - Log file missing or corrupt\n"
	       "\n"
	       "In the future we may support checking whether each file is in the log, and that\n"
	       "the file properties and map match the log, but the files found in the mounted\n"
	       "file system are not currently compared to the log\n"
	       "\n"
	       "TODO: add an option to remove bad files\n"
	       "TODO: add an option to check that all files match the log (and fix problems)\n"
	       "\n", progname);
}

int
do_famfs_cli_check(int argc, char *argv[])
{
	char *path = NULL;
	int verbose = 0;
	int arg_ct = 0;
	int rc = 0;
	int c;

	/* XXX can't use any of the same strings as the global args! */
	struct option cp_options[] = {
		/* These options set a */
		{"verbose",     no_argument,          0,  'v'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?qv",
				cp_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			famfs_check_usage(argc, argv);
			return 0;

		case 'v':
			verbose++;
			break;
		}
	}

	if (optind > (argc - 1)) {
		fprintf(stderr, "famfs_check: Must specify filename\n");
		famfs_check_usage(argc, argv);
		return EINVAL;
	}

	path = argv[optind++];

	rc = famfs_check(path, verbose);
	return rc;
}

/********************************************************************/

void
famfs_getmap_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "famfs getmap: check the validity of a famfs file, and optionally get the\n"
	       "mapping info for the file\n"
	       "\n"
	       "This command is primarily for testing and validation of a famfs file system\n"
	       "\n"
	       "    %s getmap [args] <filename>\n"
	       "\n"
	       "Arguments:\n"
	       "    -q|--quiet - Quiet print output, but exit code confirms whether the\n"
	       "                 file is famfs\n"
	       "    -?         - Print this message\n"
	       "\n"
	       "Exit codes:\n"
	       "   0    - The file is a fully-mapped famfs file\n"
	       "   1    - The file is not in a famfs file system\n"
	       "   2    - The file is in a famfs file system, but is not mapped\n"
	       " EBADF  - invalid input\n"
	       " ENOENT - file not found\n"
	       " EISDIR - File is not a regular file\n"
	       "\n"
	       "This is similar to the xfs_bmap command and is only used for testing\n"
	       "\n", progname);
}

int
do_famfs_cli_getmap(int argc, char *argv[])
{
	struct famfs_ioc_map filemap = {0};
	int c, i;
	int fd = 0;
	int rc = 0;
	char *filename = NULL;
	int arg_ct = 0;
	int quiet = 0;
	int continue_on_err = 0;
	struct stat st = { 0 };
	/* XXX can't use any of the same strings as the global args! */
	struct option cp_options[] = {
		/* These options set a */
		{"quiet",     no_argument,          0,  'q'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?qc",
				cp_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			famfs_getmap_usage(argc, argv);
			return 0;
		case 'q':
			quiet++;
			break;
		case 'c':
			continue_on_err = 1;
			break;
		}
	}

	if (optind > (argc - 1)) {
		fprintf(stderr, "famfs_getmap: Must specify filename\n");
		famfs_getmap_usage(argc, argv);
		return EINVAL;
	}
	while (optind < argc) {
		filename = argv[optind++];

		rc = stat(filename, &st);
		if (rc < 0) {
			if (!quiet)
				fprintf(stderr, "famfs_getmap: file not found (%s)\n", filename);
			rc = EBADF;
			if (!continue_on_err)
				goto err_out;
		}
		if ((st.st_mode & S_IFMT) != S_IFREG) {
			if (quiet > 1)
				fprintf(stderr, "getmap: not a regular file (%s)\n", filename);
			rc = EISDIR;
			if (continue_on_err)
				continue;

			goto err_out;
		}

		fd = open(filename, O_RDONLY, 0);
		if (fd < 0) {
			fprintf(stderr, "famfs_getmap: open failed (%s)\n", filename);
			if (continue_on_err)
				continue;

			return EBADF;
		}
		rc = ioctl(fd, FAMFSIOC_NOP, 0);
		if (rc) {
			if (!quiet)
				fprintf(stderr,
					"famfs_getmap: file (%s) not in a famfs file system\n",
					filename);
			rc = 1;
			if (continue_on_err)
				continue;

			goto err_out;
		}

		rc = ioctl(fd, FAMFSIOC_MAP_GET, &filemap);
		if (rc) {
			rc = 2;
			if (!quiet)
				printf("famfs_getmap: file (%s) is famfs, but has no map\n",
				       filename);
			if (continue_on_err)
				continue;

			goto err_out;
		}

		if (!quiet) {
			struct famfs_extent *ext_list = NULL;

			/* Only bother to retrieve extents if we'll be printing them */
			ext_list = calloc(filemap.ext_list_count, sizeof(struct famfs_extent));
			rc = ioctl(fd, FAMFSIOC_MAP_GETEXT, ext_list);
			if (rc) {
				/* If we got this far, this should not fail... */
				fprintf(stderr, "getmap: failed to retrieve ext list for (%s)\n",
					filename);
				free(ext_list);
				rc = 3;
				if (continue_on_err)
					continue;

				return rc;
			}

			printf("File:     %s\n",    filename);
			printf("\tsize:    %lld\n", filemap.file_size);
			printf("\textents: %lld\n", filemap.ext_list_count);

			for (i = 0; i < filemap.ext_list_count; i++)
				printf("\t\t%llx\t%lld\n", ext_list[i].offset, ext_list[i].len);

			free(ext_list);
		}
		printf("famfs_getmap: good file %s\n", filename);
		close(fd);
		fd = 0;
	}
err_out:
	if (fd)
		close(fd);

	return rc;
}

/********************************************************************/


void
famfs_clone_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "famfs clone: Clone a file within a famfs file system\n"
	       "\n"
	       "This administrative command is only useful in testing, and leaves the\n"
	       "file system in cross-linked state. Don't use it unless you want to generate\n"
	       "errors for testing!\n"
	       "\n"
	       "Clone a file, creating a second file with the same extent list:\n"
	       "    %s clone <src_file> <dest_file>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?           - Print this message\n"
	       "\nNOTE: this creates a file system error and is for testing only!!\n"
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

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?",
				cp_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {

		case 'v':
			verbose++;
			break;
		case 'h':
		case '?':
			famfs_clone_usage(argc, argv);
			return 0;
		}
	}

	/* There should be 2 more arguments */
	if (optind > (argc - 1)) {
		fprintf(stderr, "%s: source and destination filenames required\n", __func__);
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
	       "famfs creat: Create a file in a famfs file system\n"
	       "\n"
	       "This testing tool allocates and creates a file of a specified size.\n"
	       "\n"
	       "Create a file backed by free space:\n"
	       "    %s creat -s <size> <filename>\n\n"
	       "Create a file containing randomized data from a specific seed:\n"
	       "    %s creat -s size --randomize --seed <myseed> <filename>\n"
	       "Create a file backed by free space, with octal mode 0644:\n"
	       "    %s creat -s <size> -m 0644 <filename>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?                       - Print this message\n"
	       "    -s|--size <size>[kKmMgG] - Required file size\n"
	       "    -S|--seed <random-seed>  - Optional seed for randomization\n"
	       "    -r|--randomize           - Optional - will randomize with provided seed\n"
	       "    -m|--mode <octal-mode>   - Default is 0644\n"
	       "                               Note: mode is ored with ~umask, so the actual mode\n"
	       "                               may be less permissive; see umask for more info\n"
	       "    -u|--uid <int uid>       - Default is caller's uid\n"
	       "    -g|--gid <int gid>       - Default is caller's gid\n"
	       "    -v|--verbose             - Print debugging output while executing the command\n"
	       "\n"
	       "NOTE: the --randomize and --seed arguments are useful for testing; the file is\n"
	       "      randomized based on the seed, making it possible to use the 'famfs verify'\n"
	       "      command later to validate the contents of the file\n"
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

	size_t fsize = 0;
	s64 mult;
	int arg_ct = 0;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	mode_t mode = 0644;
	s64 seed = 0;
	int randomize = 0;
	int verbose = 0;
	mode_t current_umask;
	struct stat st;

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

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+s:S:m:u:g:rh?v",
				creat_options, &optind)) != EOF) {
		char *endptr;

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
			break;

		case 'g':
			gid = strtol(optarg, 0, 0);
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
		}
	}

	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify at least one dax device\n");
		return -1;
	}
	filename = argv[optind++];

	if (!fsize) {
		fprintf(stderr, "Non-zero file size is required\n");
		exit(-1);
	}

	rc = stat(filename, &st);
	if (rc == 0) {
		/* If the file exists and it's a regular file, and randomize is selected,
		 * we'll re-randomize the file. This is convenient for testing.
		 */
		if (!randomize) {
			fprintf(stderr, "%s: Error file exists and randomization not selected\n",
				__func__);
			exit(-1);
		}
		if ((st.st_mode & S_IFMT) != S_IFREG) {
			fprintf(stderr, "%s: Error: file %s exists and is not a regular file\n",
				__func__, filename);
			exit(-1);
		}
		fd = open(filename, O_RDWR, 0);
		if (fd < 0) {
			fprintf(stderr, "%s: Error unable to open existing file %s\n",
				__func__, filename);
			exit(-1);
		}
	} else if (rc < 0) {
		/* This is horky, but OK for the cli */
		current_umask = umask(0022);
		umask(current_umask);
		mode &= ~(current_umask);
		fd = famfs_mkfile(filename, mode, uid, gid, fsize, verbose);
		if (fd < 0) {
			fprintf(stderr, "%s: failed to create file %s\n", __func__, filename);
			exit(-1);
		}
	}
	if (randomize) {
		struct stat st;
		void *addr;
		char *buf;

		rc = fstat(fd, &st);
		if (rc) {
			fprintf(stderr, "%s: failed to stat newly craeated file %s\n",
				__func__, filename);
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
		flush_processor_cache(buf, fsize);
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
	       "famfs mkdir: Create a directory in a famfs file system:\n"
	       "\n"
	       "    %s mkdir [args] <dirname>\n\n"
	       "\n"
	       "Arguments:\n"
	       "    -?               - Print this message\n"
	       "    -p|--parents     - No error if existing, make parent directories as needed,\n"
	       "                       the -m option only applies to dirs actually created\n"
	       "    -m|--mode=<mode> - Set mode (as in chmod) to octal value\n"
	       "    -u|--uid=<uid>   - Specify uid (default is current user's uid)\n"
	       "    -g|--gid=<gid>   - Specify uid (default is current user's gid)\n"
	       "    -v|--verbose     - Print debugging output while executing the command\n",
	       progname);
}

int
do_famfs_cli_mkdir(int argc, char *argv[])
{
	int c;

	mode_t mode = 0644;
	char *dirpath   = NULL;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	int arg_ct = 0;
	int parents = 0;
	int verbose = 0;
	mode_t current_umask;

	/* TODO: allow passing in uid/gid/mode on command line*/

	/* XXX can't use any of the same strings as the global args! */
	struct option mkdir_options[] = {
		/* These options set a flag. */

		/* These options don't set a flag.
		 * We distinguish them by their indices.
		 */
		/*{"dryrun",       no_argument,       0, 'n'}, */
		{"parents",      no_argument,         0,  'p'},
		{"mode",        required_argument,    0,  'm'},
		{"uid",         required_argument,             0,  'u'},
		{"gid",         required_argument,             0,  'g'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+pvm:u:g:h?",
				mkdir_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {

		case 'h':
		case '?':
			famfs_mkdir_usage(argc, argv);
			return 0;

		case 'p':
			parents++;
			break;

		case 'm':
			mode = strtol(optarg, 0, 8); /* Must be valid octal */
			break;

		case 'u':
			uid = strtol(optarg, 0, 0);
			break;

		case 'g':
			gid = strtol(optarg, 0, 0);
			break;

		case 'v':
			verbose++;
			break;
		}
	}

	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify at least one dax device\n");
		return -1;
	}

	/* This is horky, but OK for the cli */
	current_umask = umask(0022);
	umask(current_umask);
	mode &= ~(current_umask);

	dirpath  = argv[optind++];
	if (parents)
		return famfs_mkdir_parents(dirpath, mode, uid, gid, verbose);

	return famfs_mkdir(dirpath, mode, uid, gid, verbose);
}

/********************************************************************/
void
famfs_verify_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "famfs verify: Verify the contents of a file that was created with 'famfs creat':\n"
	       "    %s verify -S <seed> -f <filename>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?                        - Print this message\n"
	       "    -f|--filename <filename>  - Required file path\n"
	       "    -S|--seed <random-seed>   - Required seed for data verification\n"
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
	s64 rc = 0;

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
		/* These options set a */
		{"seed",        required_argument,             0,  'S'},
		{"filename",    required_argument,             0,  'f'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+f:S:h?",
				map_options, &optind)) != EOF) {

		arg_ct++;
		switch (c) {

		case 'S':
			seed = strtoull(optarg, 0, 0);
			break;

		case 'f': {
			filename = optarg;
			/* TODO: make sure filename is in a famfs file system */
			break;
		}
		case 'h':
		case '?':
			famfs_verify_usage(argc, argv);
			return 0;
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
	fd = open(filename, O_RDWR, 0);
	if (fd < 0) {
		fprintf(stderr, "open %s failed; rc %lld errno %d\n", filename, rc, errno);
		exit(-1);
	}

	addr = famfs_mmap_whole_file(filename, 0, &fsize);
	if (!addr) {
		fprintf(stderr, "%s: randomize mmap failed\n", __func__);
		exit(-1);
	}
	invalidate_processor_cache(addr, fsize);
	buf = (char *)addr;
	rc = validate_random_buffer(buf, fsize, seed);
	if (rc == -1) {
		printf("Success: verified %ld bytes in file %s\n", fsize, filename);
	} else {
		fprintf(stderr, "Verify fail at offset %lld of %ld bytes\n", rc, fsize);
		exit(-1);
	}

	return 0;
}

/********************************************************************/

void hex_dump(const u8 *adr, size_t len, const char *str)
{

	/*unsigned int i,byte;*/
	int i;
	int ctr;

	assert(len > 0);

	ctr = 0;

	printf("%s\n", str);
	while (ctr < len) {
		/*      printf("%8x - ",ptr); */
		for (i = 0; i < 16; i++, ctr++) {
			if (ctr >= len)
				break;

			printf("%02x ", adr[ctr]);
		}
		printf("\n");
	}
}


void
famfs_chkread_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "famfs chkread: verify that the contents of a file match via read and mmap\n\n"
	       "    %s chkread <famfs-file>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?  - Print this message\n"
	       "    -s  - File is famfs superblock\n"
	       "    -l  - File is famfs log\n"
	       "\n", progname);
}

/**
 * famfs_chkread()
 *
 * This function was added while debugging some dragons in /dev/dax resolution of
 * faults vs. read/write, and it's a useful test. It just verifies that the contents
 * of a file are the same whether accessed by read or mmap
 */
int
do_famfs_cli_chkread(int argc, char *argv[])
{
	int c, fd;
	char *filename = NULL;
	int is_log = 0;
	int is_superblock = 0;
	size_t fsize = 0;
	int arg_ct = 0;
	void *addr;
	char *buf;
	int rc = 0;
	char *readbuf = NULL;
	struct stat st;

	/* XXX can't use any of the same strings as the global args! */
	struct option map_options[] = {
		/* These options set a */
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+slh?",
				map_options, &optind)) != EOF) {
		arg_ct++;
		switch (c) {
		case 'h':
		case '?':
			famfs_chkread_usage(argc, argv);
			return 0;
		case 's':
			is_superblock = 1;
			break;
		case 'l':
			is_log = 1;
			break;
		}
	}
	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify at least one file\n");
		return -1;
	}
	filename = argv[optind++];

	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		exit(-1);
	}

	rc = stat(filename, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: could not stat file %s\n", __func__, filename);
		exit(-1);
	}
	buf = calloc(1, st.st_size);
	assert(buf);

	fd = open(filename, O_RDWR, 0);
	assert(fd > 0);


	addr = famfs_mmap_whole_file(filename, 0, &fsize);
	assert(addr);

	rc = lseek(fd, 0, SEEK_SET);
	assert(rc == 0);

	rc = posix_memalign((void **)&readbuf, 0x200000, fsize);
	assert(rc == 0);
	assert(readbuf);
	printf("readbuf: %p\n", readbuf);

	rc = read(fd, readbuf, fsize);
	assert(rc == fsize);

	printf("read tried=%d got=%d\n", (int)fsize, (int)rc);
	if (is_superblock) {
		printf("superblock by mmap\n");
		famfs_dump_super((struct famfs_superblock *)addr);
		printf("superblock by read\n");
		famfs_dump_super((struct famfs_superblock *)readbuf);

		hex_dump((const u8 *)addr, 32, "Superblock by mmap");
		hex_dump((const u8 *)readbuf, 32, "Superblock by read");
	}
	if (is_log) {
		printf("Log by mmap\n");
		famfs_dump_log((struct famfs_log *)addr);
		printf("Log by read\n");
		famfs_dump_log((struct famfs_log *)readbuf);

		hex_dump((const u8 *)addr, 64, "Log by mmap");
		hex_dump((const u8 *)readbuf, 64, "Log by read");
	}
	rc = memcmp(readbuf, addr, fsize);
	if (rc) {
		fprintf(stderr, "Read and mmap miscompare\n");
		rc = -1;
		goto err_exit;
	}
	printf("Read and mmap match\n");

 err_exit:
	if (buf)
		free(buf);
	if (readbuf)
		free(readbuf);
	return rc;
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

	{"mount",   do_famfs_cli_mount,   famfs_mount_usage},
	{"fsck",    do_famfs_cli_fsck,    famfs_fsck_usage},
	{"check",   do_famfs_cli_check,   famfs_check_usage},
	{"mkdir",   do_famfs_cli_mkdir,   famfs_mkdir_usage},
	{"cp",      do_famfs_cli_cp,      famfs_cp_usage},
	{"creat",   do_famfs_cli_creat,   famfs_creat_usage},
	{"verify",  do_famfs_cli_verify,  famfs_verify_usage},
	{"mkmeta",  do_famfs_cli_mkmeta,  famfs_mkmeta_usage},
	{"logplay", do_famfs_cli_logplay, famfs_logplay_usage},
	{"getmap",  do_famfs_cli_getmap,  famfs_getmap_usage},
	{"clone",   do_famfs_cli_clone,   famfs_clone_usage},
	{"chkread", do_famfs_cli_chkread, famfs_chkread_usage},

	{NULL, NULL, NULL}
};

static void
do_famfs_cli_help(int argc, char **argv)
{
	int i;
	char *progname = basename(argv[0]);
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
	int rc;

	/* Process global options, if any */
	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?:",
				global_options, &optind)) != EOF) {

		switch (c) {
		case 'h':
		case '?':
			do_famfs_cli_help(argc, argv);
			return 0;
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
			rc = famfs_cli_cmds[i].run(argc, argv);
			return rc;
		}
	}

	fprintf(stderr, "famfs cli: Unrecognized command %s\n", argv[optind]);
	do_famfs_cli_help(argc, argv);

	return -1;
}

