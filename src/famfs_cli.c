// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
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
#include "thpool.h"
#include "famfs_log.h"

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

static void verbose_to_log_level(int verbose)
{
	if (verbose == 1) {
	    famfs_log_set_level(FAMFS_LOG_INFO);
	    return;
	}
	if (verbose > 1)
	    famfs_log_set_level(FAMFS_LOG_DEBUG);
}

/********************************************************************/

void
famfs_logplay_usage(int argc, char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs logplay: Play the log of a mounted famfs file system\n"
	       "\n"
	       "This administrative command is necessary if files have been added by another node\n"
	       "since the file system was mounted (or since the last logplay)\n"
	       "\n"
	       "    %s logplay [args] <mount_point>\n"
	       "\n"
	       "Arguments:\n"
	       "    -n|--dryrun  - Process the log but don't instantiate the files & directories\n"
	       "    -v|--verbose - Verbose output\n"
	       "\n"
	       "\n",
	       progname);
}

int
do_famfs_cli_logplay(int argc, char *argv[])
{
	int c;
	int rc = 0;
	char *fspath;
	int verbose = 0;
	int dry_run = 0;
	int use_mmap = 0;
	int use_read = 0;
	int shadowtest = 0;
	int client_mode = 0;
	char *daxdev = NULL;
	extern int mock_fstype;
	char *shadowpath = NULL;

	struct option logplay_options[] = {
		/* Public options */
		{"dryrun",    no_argument,             0,  'n'},
		{"verbose",   no_argument,             0,  'v'},

		/* These options are for testing and are not listed
		 * in the help above */
		{"mmap",      no_argument,             0,  'm'},
		{"read",      no_argument,             0,  'r'},
		{"client",    no_argument,             0,  'c'},
		{"shadowtest", no_argument,            0,  's'},
		{"shadow",    required_argument,       0,  'S'},
		{"daxdev",    required_argument,       0,  'd'},
		{"mock",      no_argument,             0,  'M'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vrcmnhSd:?M",
				logplay_options, &optind)) != EOF) {

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
		case 'S':
			if (shadowpath) {
				fprintf(stderr,
					"%s: don't specify more than one "
					"shadowpath\n",
					__func__);
				return EINVAL;
			}
			shadowpath = optarg;
			break;
		case 's':
			shadowtest = 1;
			break;
		case 'd':
			daxdev = optarg;
			break;
		case 'M':
			mock_fstype = FAMFS_V1;
			break;
		}
	}

	if (use_mmap && use_read) {
		fprintf(stderr,
			"Error: The --mmap and --read arguments "
			"are mutually exclusive\n\n");
		famfs_logplay_usage(argc, argv);
		return 1;
	}
	if (!(use_mmap || use_read)) {
		/* If neither method was explicitly requested, default to mmap */
		use_mmap = 1;
	}

	if (daxdev && !shadowpath) {
		fprintf(stderr,
			"Error: daxdev only used with shadow logplay\n");
		return 1;
	}
	if (shadowpath)
		printf("Logplay: running in shadow test mode\n");

	/* If there is no --daxdev, the mount point is required */
	if (!daxdev && optind > (argc - 1)) {
		fprintf(stderr, "Must specify mount_point "
			"(actually any path within a famfs file system "
			"will work)\n");
		famfs_logplay_usage(argc, argv);
		return 1;
	}
	fspath = argv[optind++];

	if (daxdev)
		rc = famfs_dax_shadow_logplay(shadowpath, dry_run,
					      client_mode, daxdev,
					      shadowtest, verbose);
	else
		rc = famfs_logplay(fspath, use_mmap, dry_run, client_mode,
				   shadowpath, shadowtest, verbose);
	if (rc == 0)
		famfs_log(FAMFS_LOG_NOTICE,
			  "famfs cli: famfs logplay completed successfully on %s", fspath);
	else
		famfs_log(FAMFS_LOG_ERR, "famfs cli: famfs logplay failed on %s", fspath);

	return rc;
}

/********************************************************************/
void
famfs_mount_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs mount: mount a famfs file system and make it ready to use\n"
	       "\n"
	       "    %s mount [args] <memdevice> <mountpoint>\n"
	       "\n"
	       "Arguments:\n"
	       "    -h|-?              - Print this message\n"
	       "    -f|--fuse          - Use famfs via fuse. If specified, the mount will\n"
	       "                         fail if fuse support for famfs is not available.\n"
	       "    -F|--nofuse        - Use the standalone famfs v1 kernel module. If\n"
	       "                         specified, the mount will fail if the famfs v1\n"
	       "                         kernel module is not available\n"
	       "    -t|--timeout       - Fuse metadata timeout in seconds\n"
	       "    -d|--debug         - In fuse mode, the debug option runs the fuse\n"
	       "                         daemon single-threaded, and may enable more\n"
	       "                         verbose logging\n"
	       "    -v|--verbose       - Print verbose output\n"
	       "    -u|--nouseraccess  - Allow non-root access\n"
	       "                         (don't use fuse allow_other mount opt)\n"
	       "    -p|--nodefaultperm - Do not apply normal posix permissions\n"
	       "                         (don't use fuse default_permissions mount opt)\n"
	       "    -S|--shadow=path   - Path to root of shadow filesystem\n"
	       "    -b|--bouncedax     - Disable and re-enable the primary daxdev prior to mount\n"
	       "                         (fuse only)\n"
	       "\n", progname);
}

int
do_famfs_cli_mount(int argc, char *argv[])
{
	int c;
	int rc;
	int dummy = 0;
	int debug = 0;
	int verbose = 0;
	int use_read = 0;
	int bouncedax = 0;
	int useraccess = 1;
	int default_perm = 1;
	char *shadowpath = NULL;
	int use_mmap = 0;
	char *mpt = NULL;
	int fuse_mode = 0;
	int remaining_args;
	char *daxdev = NULL;
	char *realmpt = NULL;
	ssize_t timeout = -1;
	char *cachearg = NULL;
	char *realdaxdev = NULL;
	const char *famfs_mode = getenv("FAMFS_MODE");
	unsigned long mflags = MS_NOATIME | MS_NOSUID | MS_NOEXEC | MS_NODEV;

	struct option mount_options[] = {
		/* These options set a */
		{"read",       no_argument,            0,  'r'},
		{"mmap",       no_argument,            0,  'm'},
		{"debug",      no_argument,            0,  'd'},
		{"fuse",       no_argument,            0,  'f'},
		{"nofuse",     no_argument,            0,  'F'},
		{"timeout",    required_argument,      0,  't'},
		{"verbose",    no_argument,            0,  'v'},
		{"nouseraccess", no_argument,          0,  'u'},
		{"nodefaultperm", no_argument,         0,  'p'},
		{"bouncedax",   no_argument,           0,  'b'},
		{"shadow",     required_argument,      0,  'S'},
		{"dummy",      no_argument,            0,  'D'},

		/* un-advertised options */
		{"remount",    no_argument,            0,  'R'},
		{"cache",      required_argument,      0,  'c'},
		{0, 0, 0, 0}
	};

	if (famfs_mode)
		printf("%s: FAMFS_MODE=%s (ignored)\n", __func__, famfs_mode);

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?RrfFmvupbdt:c:S:D",
				mount_options, &optind)) != EOF) {

		switch (c) {

		case 'h':
		case '?':
			famfs_mount_usage(argc, argv);
			return 0;
		case 'v':
			verbose++;
			break;
		case 'd':
			debug++;
			break;
		case 'm':
			use_mmap = 1;
			break;
		case 'r':
			fprintf(stderr,
				"%s: warning: the read option can cause "
				"cache coherency problems\n", __func__);
			use_read = 1;
			break;
		case 'R':
			mflags |= MS_REMOUNT;
			break;
		case 'f':
			fuse_mode = FAMFS_FUSE;
			break;
		case 'F':
			fuse_mode = FAMFS_V1;
			break;
		case 'u':
			useraccess = 0;
			break;
		case 'p':
			default_perm = 0;
			break;
		case 'S':
			if (shadowpath) {
				fprintf(stderr,
					"%s: don't specify more than one "
					"shadowpath\n", __func__);
				return -EINVAL;
			}
			shadowpath = optarg;
			break;
		case 't':
			timeout = strtoul(optarg, 0, 0);
			break;
		case 'c':
			/* This was inherited from passthrough_ll.c (libfuse)
			 * and will likely be removed soon */
			cachearg = optarg;
			break;
		case 'D':
			printf("dummy = 1\n");
			dummy = 1;
			break;
		}
	}

	if (use_mmap && use_read) {
		fprintf(stderr,
			"Error: --mmap and --read are mutually exclusive\n\n");
		famfs_mount_usage(argc, argv);
		return -1;
	} else if (!(use_mmap || use_read)) {
		/* If neither method was explicitly requested, default to mmap */
		use_mmap = 1;
	}
	if (timeout != -1 && cachearg) {
		fprintf(stderr,
			"%s: Error: timeout & cache args mutually exclusive\n",
			__func__);
		return -1;
	} else if (cachearg) {
		if (strcmp(cachearg, "always") == 0)
			timeout = 3600 * 24 * 365;
		else if (strcmp(cachearg, "normal") == 0)
			timeout = 1;
		else if (strcmp(cachearg, "never") == 0)
			timeout = 0;
		else {
			fprintf(stderr, "%s: invalid arg cache=%s\n",
				__func__, cachearg);
			return -1;
		}
	}
	if (timeout == -1)
		timeout = 3600 * 24 * 365;

	remaining_args = argc - optind;

	if (dummy && remaining_args != 1) {
		fprintf(stderr,
			"%s: error: dummy mount requires <daxdev>\n",
			__func__);
		famfs_mount_usage(argc, argv);
		return -1;
	}
	else if (!dummy && remaining_args != 2) {
		fprintf(stderr,
			"%s: error: <daxdev> and <mountpoint> args required\n",
			__func__);
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
	if (!dummy) {
		mpt = argv[optind++];
		realmpt = realpath(mpt, NULL);
		if (!realmpt) {
			fprintf(stderr,
				"famfs mount: mount pt (%s) not found\n", mpt);
			free(realmpt);
			rc = -1;
			goto err_out;
		}
	}

	if (fuse_mode == 0)
		fuse_mode = famfs_get_kernel_type(verbose);
	if (fuse_mode == NOT_FAMFS) {
		fprintf(stderr, "%s: kernel not famfs-enabled\n", __func__);
		rc = -1;
		goto err_out;
	}

	if (verbose)
		verbose_to_log_level(verbose);

	if (fuse_mode == FAMFS_FUSE) {
		if (dummy) {
			char *mpt_out;
			rc = famfs_dummy_mount(realdaxdev,
					       0 /* figure out log size */,
					       &mpt_out,
					       debug, verbose);
			if (rc == 0)
				printf("Successful dummy mount at %s\n",
				       mpt_out);

			free(mpt_out);
			goto out;
		}

		printf("daxdev=%s, mpt=%s\n", realdaxdev, realmpt);
		rc = famfs_mount_fuse(realdaxdev, realmpt, shadowpath,
				      timeout, use_mmap, useraccess,
				      default_perm, bouncedax,
				      0, 0, /* not dummy mount */
				      debug, verbose);
		goto out;

	}

	/*
	 * From here down, it's a standalone famfs mount
	 */

	if (dummy) {
		fprintf(stderr, "famfs mount: dummy mode is fuse-only\n");
		rc = -1;
		goto err_out;
	}
	if (!famfs_module_loaded(1)) {
		fprintf(stderr,
			"famfs mount: famfs kernel module is not loaded!\n");
		fprintf(stderr, "famfs mount: try 'sudo modprobe famfs'\n");
		rc = -1;
		goto err_out;
	}

	/* This functions as a verification that the daxdev contains a valid
	 * famfs file system. Need to fail out before calling the system mount()
	 * if it's not a valid famfs file system.
	 */
	rc = famfs_get_role_by_dev(realdaxdev);
	if (rc < 0 || rc == FAMFS_NOSUPER) {
		fprintf(stderr,
			"famfs mount: failed to validate famfs file system\n");
		rc = -1;
		goto err_out;
	}

	if (bouncedax) {
		rc = famfs_bounce_daxdev(realdaxdev, verbose);
		if (rc) {
			fprintf(stderr, "%s: failed to bounce daxdev %s\n",
				__func__, realdaxdev);
			return rc;
		}
	}

	rc = mount(realdaxdev, realmpt, "famfs", mflags, "");
	if (rc) {
		fprintf(stderr,
			"famfs mount: mount returned %d; errno %d\n",
			rc, errno);
		perror("mount fail\n");
		goto err_out;
	}

	rc = famfs_mkmeta_standalone(realdaxdev, verbose);
	if (rc) {
		fprintf(stderr,
			"famfs mount: err %d from mkmeta; unmounting\n", rc);
		umount(realmpt);
		goto err_out;
	}
	/*
	 * No more access allowed to the raw daxdev after mkmeta!
	 */

	rc = famfs_logplay(realmpt, use_mmap,
			   0    /* not dry-run */,
			   0    /* not client-mode */,
			   NULL /* no shadow path */,
			   0    /* not shadow-test */,
			   verbose);
	if (rc == 0)
		famfs_log(FAMFS_LOG_NOTICE,
			  "famfs cli: famfs mount completed successfully on %s",
			  realmpt);
	else
		famfs_log(FAMFS_LOG_ERR, "famfs cli: famfs mount failed on %s",
			  realmpt);

out:
err_out:
	free(realdaxdev);
	free(realmpt);
	return rc;
}

/********************************************************************/

void
famfs_mkmeta_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs mkmeta:\n"
	       "\n"
	       "This legacy command is only used during testing (and only for \"standalone\"\n"
	       "famfs, which will be deprecated soon). The famfs mount procedure\n"
	       "automatically creates the meta files for you.\n"
	       "\n"
	       "The famfs file system exposes its superblock and log to its userspace components\n"
	       "as files, and other famfs cli commands (e.g. fsck, logplay) access the superblock\n"
	       "via their meta files.\n"
	       "\n"
	       "    %s mkmeta <memdevice>  # Example memdevice: /dev/dax0.0\n"
	       "\n"
	       "Arguments:\n"
	       "    -h|-?            - Print this message\n"
	       "    -v|--verbose     - Print verbose output\n"
	       "\n", progname);
}

int
do_famfs_cli_mkmeta(int argc, char *argv[])
{
	char *realdaxdev = NULL;
	char *daxdev = NULL;
	int verbose = 0;
	int c;

	struct option mkmeta_options[] = {
		/* These options set a */
		{"verbose",    no_argument,            0,  'v'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?",
				mkmeta_options, &optind)) != EOF) {

		switch (c) {
		case 'v':
			verbose++;
			break;
		case 'h':
		case '?':
			famfs_mkmeta_usage(argc, argv);
			return 0;
		}
	}

	if (optind > (argc - 1)) {
		fprintf(stderr, "%s: Must specify at least one dax device\n",
			__func__);
		famfs_mkmeta_usage(argc, argv);
		return -1;
	}

	daxdev = argv[optind++];
	realdaxdev = realpath(daxdev, NULL);
	if (!realdaxdev) {
		fprintf(stderr,
			"%s: unable to rationalize daxdev path from "
			"(%s) rc %d\n", __func__, daxdev, errno);
		free(realdaxdev);
		return -1;
	}
	famfs_mkmeta_standalone(realdaxdev, verbose);
	free(realdaxdev);
	return 0;
}

/********************************************************************/

void
famfs_fsck_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs fsck: check a famfs file system\n"
	       "\n"
	       "This command checks the validity of the superblock and log, and scans the\n"
	       "log for cross-linked files.\n"
	       "\n"
	       "Check an unmounted famfs file system\n"
	       "    %s fsck [args] <memdevice>  # Example memdevice: /dev/dax0.0\n"
	       "\n"
	       "Check a mounted famfs file system:\n"
	       "    %s [args] <mount point>\n"
	       "\n"
	       "Arguments:\n"
	       "    -?           - Print this message\n"
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
	extern int mock_fstype;
	char *daxdev = NULL;
	bool nodax = false;
	int nbuckets = 0;
	int use_mmap = 0;
	int use_read = 0;
	int verbose = 0;
	int force = 0;
	int human = 0; /* -h is no longer --help... */

	struct option fsck_options[] = {
		{"human",       no_argument,             0,  'h'},
		{"verbose",     no_argument,             0,  'v'},
		{"force",       no_argument,             0,  'f'},
		{"nbuckets",    required_argument,       0,  'B'},

		/* Un-publicized options */
		{"mmap",        no_argument,             0,  'm'},
		{"read",        no_argument,             0,  'r'},
		{"mock",        no_argument,             0,  'M'},
		{"nodax",       no_argument,             0,  'D'},

		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?mrfMB:D",
				fsck_options, &optind)) != EOF) {

		switch (c) {
		case 'm':
			use_mmap = 1;
			break;
		case 'r':
			fprintf(stderr,
				"%s: warning: the read option can cause "
				"cache coherency problems\n",
				__func__);
			use_read = 1;
			break;
		case 'h':
			human = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'f':
			/* This "hidden" option tries to mmap devdax
			 * even when a fs is mounted */
			force++;
			break;
		case 'M':
			mock_fstype = FAMFS_V1;
			break;
		case 'B':
			nbuckets = strtoul(optarg, 0, 0);
			break;
		case 'D':
			nodax = true;
			break;
		case '?':
			famfs_fsck_usage(argc, argv);
			return 0;
		}
	}

	if (use_mmap && use_read) {
		fprintf(stderr,
			"Error: The --mmap and --read arguments "
			"are mutually exclusive\n\n");
		famfs_fsck_usage(argc, argv);
		return -1;
	} else if (!(use_mmap || use_read)) {
		/* If neither method was explicitly requested, default to mmap */
		use_mmap = 1;
	}
	if (optind > (argc - 1)) {
		fprintf(stderr, "%s: Must specify at least one dax device\n",
			__func__);
		famfs_fsck_usage(argc, argv);
		return -1;
	}

	daxdev = argv[optind++];
	return famfs_fsck(daxdev, nodax, use_mmap, human, force,
			  nbuckets, verbose);
}


/********************************************************************/

void
famfs_cp_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

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
	       "Arguments:\n"
	       "    -h|-?                         - Print this message\n"
	       "    -r                            - Recursive\n"
	       "    -t|--threadct <nthreads>      - Number of copy threads\n"
	       "    -m|--mode <mode>              - Set mode (as in chmod) to octal value\n"
	       "    -u|--uid <uid>                - Specify uid (default is current user's uid)\n"
	       "    -g|--gid <gid>                - Specify uid (default is current user's gid)\n"
	       "    -v|--verbose                  - print debugging output while executing the command\n"
	       "Interleaving Arguments:\n"
	       "    -N|--nstrips <n>              - Number of strips to use in interleaved allocations.\n"
	       "    -B|--nbuckets <n>             - Number of buckets to divide the device into\n"
	       "                                    (nstrips && nbuckets) causes strided\n"
	       "                                    allocation within a single device.\n"
	       "    -C|--chunksize <size>[kKmMgG] - Size of chunks for interleaved allocation\n"
	       "                        (default=2M)\n"
	       "\n"
	       "NOTE 1: 'famfs cp' will only overwrite an existing file if it the correct size.\n"
	       "        This makes 'famfs cp' restartable if necessary.\n"
	       "NOTE 2: you need this tool to copy a file into a famfs file system,\n"
	       "        but the standard \'cp\' can be used to copy FROM a famfs file system.\n"
	       "\n",
	       progname, progname, progname);
}

int
do_famfs_cli_cp(int argc, char *argv[])
{
	struct famfs_interleave_param interleave_param = { 0 };
	uid_t uid = getuid();
	gid_t gid = getgid();
	mode_t current_umask;
	int remaining_args;
	int recursive = 0;
	int verbose = 0;
	mode_t mode = 0; /* null mode inherits mode form source file */
	int rc;
	int c;
	int set_stripe = 0;
	s64 mult;
	int thread_ct = 0;

	extern int cp_compare;

	interleave_param.chunk_size = 0x200000; /* 2MiB default chunk */

	struct option cp_options[] = {
		/* These options set a */
		{"mode",        required_argument,    0,  'm'},
		{"uid",         required_argument,    0,  'u'},
		{"gid",         required_argument,    0,  'g'},
		{"verbose",     no_argument,          0,  'v'},
		{"recursive",   no_argument,          0,  'r'},

		{"threadct",    required_argument,    0,  't'},
		{"compare",     no_argument,          0,  'c'},

		{"chunksize",   required_argument,    0,  'C'},
		{"nstrips",     required_argument,    0,  'N'},
		{"nbuckets",    required_argument,    0,  'B'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+rm:u:g:C:N:B:vt:ch?",
				cp_options, &optind)) != EOF) {

		char *endptr;
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
		case 't':
			thread_ct = strtol(optarg, 0, 0);
			break;
		case 'c':
			cp_compare = 1;
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

		case 'C':
			set_stripe++;
			interleave_param.chunk_size = strtoull(optarg,
							       &endptr, 0);
			mult = get_multiplier(endptr);
			if (mult > 0)
				interleave_param.chunk_size *= mult;
			break;

		case 'N':
			set_stripe++;
			interleave_param.nstrips = strtoull(optarg, 0, 0);
			break;

		case 'B':
			set_stripe++;
			interleave_param.nbuckets = strtoull(optarg, 0, 0);
			break;
		}
	}

	remaining_args = argc - optind;

	if (remaining_args < 2) {
		fprintf(stderr,
			"famfs cp error: source and dest args required\n");
		famfs_cp_usage(argc, argv);
		return -1;
	}
	if (set_stripe && interleave_param.nstrips > FAMFS_MAX_SIMPLE_EXTENTS) {
		fprintf(stderr,
			"famfs cp error: nstrips(%lld) > %d \n",
			interleave_param.nstrips, FAMFS_MAX_SIMPLE_EXTENTS);
		return -1;
	}

	struct famfs_interleave_param *s = (set_stripe) ? &interleave_param : NULL;

	/* This is horky, but OK for the cli */
	current_umask = umask(0022);
	umask(current_umask);
	mode &= ~(current_umask);

	rc = famfs_cp_multi(argc - optind, &argv[optind], mode, uid, gid,
			    s, recursive, thread_ct, verbose);
	return rc;
}


/********************************************************************/

void
famfs_check_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs check: check the contents of a famfs file system.\n"
	       "\n"
	       "NOTE: 'famfs check' is only useful for standalone famfs. For fuse-based\n"
	       "      famfs, a new 'famfs logplay --check' option will be added to run\n"
	       "      appropriate checks for famfs-fuse\n"
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
	       "    -h|-?        - Print this message\n"
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
	int rc = 0;
	int c;

	struct option check_options[] = {
		/* These options set a */
		{"verbose",     no_argument,          0,  'v'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?qv",
				check_options, &optind)) != EOF) {

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
famfs_getmap_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

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
	       "    -h|-?      - Print this message\n"
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
	int continue_on_err = 0;
	struct stat st = { 0 };
	char *filename = NULL;
	int quiet = 0;
	int fd = 0;
	int rc = 0;
	int c;
	u32 i;

	struct option getmap_options[] = {
		/* These options set a */
		{"quiet",     no_argument,          0,  'q'},
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+h?qc",
				getmap_options, &optind)) != EOF) {

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
				fprintf(stderr,
					"famfs_getmap: file not found (%s)\n",
					filename);
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
					"famfs_getmap: file (%s) not in famfs\n",
					filename);
			rc = 1;
			if (continue_on_err)
				continue;

			goto err_out;
		}

		if (FAMFS_KABI_VERSION > 42) {
#if (FAMFS_KABI_VERSION > 42)
			struct famfs_ioc_get_fmap ifmap;

			/* In v2 we get the whole thing in one ioctl */
			rc = ioctl(fd, FAMFSIOC_MAP_GET_V2, &ifmap);
			if (rc) {
				rc = 2;
				if (!quiet)
					printf("famfs_getmap: file (%s) is famfs, but has no map\n",
					       filename);
				if (continue_on_err)
					continue;

				goto err_out;
			}

			if (quiet)
				goto next_file;

			printf("File:     %s\n",    filename);
			printf("\tsize:    %lld\n", ifmap.iocmap.fioc_file_size);
			printf("\textents: %d\n", ifmap.iocmap.fioc_nextents);

			switch (ifmap.iocmap.fioc_ext_type) {
			case FAMFS_IOC_EXT_SIMPLE:
				/* XXX
				 * Note currently not printing devindex because
				 * it's always 0 */
				for (i = 0; i < ifmap.iocmap.fioc_nextents; i++)
					printf("\t\t%llx\t%lld\n",
					       ifmap.ikse[i].offset,
					       ifmap.ikse[i].len);
				break;

			case FAMFS_IOC_EXT_INTERLEAVE: {
				struct famfs_ioc_simple_extent *strips;

				strips = ifmap.ks.kie_strips;
				printf("Interleave_Param chunk_size: %lld\n",
				       ifmap.ks.ikie.ie_chunk_size);
				printf("Interleaved extent has %lld strips:\n",
				       ifmap.ks.ikie.ie_nstrips);

				/* XXX
				 * Note currently not printing devindex because
				 * it's always 0 */
				for (i = 0; i < ifmap.ks.ikie.ie_nstrips; i++)
					printf("\t\t%llx\t%lld\n",
					       strips[i].offset, strips[i].len);

				break;
			}
			}
#endif
		} else {
			struct famfs_extent *ext_list = NULL;

			rc = ioctl(fd, FAMFSIOC_MAP_GET, &filemap);
			if (rc) {
				rc = 2;
				if (!quiet)
					printf("famfs_getmap: file (%s) is famfs, but has no map\n",
					       filename);
				if (continue_on_err)
					continue;

				goto next_file;
			}

			if (quiet)
				goto next_file;

			/* Only bother to retrieve extents if we'll be
			 * printing them */
			ext_list = calloc(filemap.ext_list_count,
					  sizeof(struct famfs_extent));
			rc = ioctl(fd, FAMFSIOC_MAP_GETEXT, ext_list);
			if (rc) {
				/* If we got this far, this should not fail... */
				fprintf(stderr,
					"getmap: failed to retrieve ext list for (%s)\n",
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
				printf("\t\t%llx\t%lld\n",
				       ext_list[i].offset, ext_list[i].len);

			free(ext_list);
		}
next_file:
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
famfs_clone_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

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
	       "    -h|-?        - Print this message\n"
	       "\nNOTE: this creates a file system error and is for testing only!!\n"
	       "\n", progname);
}

int
do_famfs_cli_clone(int argc, char *argv[])
{
	int c;
	int verbose = 0;
	char *srcfile = NULL;
	char *destfile = NULL;
	char srcfullpath[PATH_MAX];

	struct option clone_options[] = {
		/* These options set a */
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?",
				clone_options, &optind)) != EOF) {

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
	if (optind > (argc - 2)) {
		fprintf(stderr,
			"%s: source and destination filenames required\n",
			__func__);
		famfs_clone_usage(argc, argv);
		return -1;
	}
	srcfile  = argv[optind++];
	destfile = argv[optind++];
	if (realpath(srcfile, srcfullpath) == NULL) {
		fprintf(stderr, "%s: bad source path %s\n", __func__, srcfile);
		return -1;
	}

	return famfs_clone(srcfile, destfile);
}

/********************************************************************/

void
famfs_creat_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs creat: Create a file in a famfs file system\n"
	       "\n"
	       "This tool allocates and creates files.\n"
	       "\n"
	       "Create a file backed by free space:\n"
	       "    %s creat -s <size> <filename>\n"
	       "\n"
	       "Create a file containing randomized data from a specific seed:\n"
	       "    %s creat -s size --randomize --seed <myseed> <filename>\n"
	       "\n"
	       "Create a file backed by free space, with octal mode 0644:\n"
	       "    %s creat -s <size> -m 0644 <filename>\n"
	       "\n"
	       "Create two files randomized with separte seeds:\n"
	       "    %s creat --multi file1,256M,42 --multi file2,256M,43\n"
	       "\n"
	       "Create two non-randomized files:\n"
	       "    %s creat --multi file1,256M --multi file2,256M\n"
	       "\n"
	       "Arguments:\n"
	       "    -h|-?                    - Print this message\n"
	       "    -m|--mode <octal-mode>   - Default is 0644\n"
	       "                               Note: mode is ored with ~umask, so the actual mode\n"
	       "                               may be less permissive; see umask for more info\n"
	       "    -u|--uid <int uid>       - Default is caller's uid\n"
	       "    -g|--gid <int gid>       - Default is caller's gid\n"
	       "    -v|--verbose             - Print debugging output while executing the command\n"
	       "\n"
	       "Single-file create: (cannot mix with multi-create)\n"
	       "    -s|--size <size>[kKmMgG] - Required file size\n"
	       "    -S|--seed <random-seed>  - Optional seed for randomization\n"
	       "    -r|--randomize           - Optional - will randomize with provided seed\n"
	       "\n"
	       "Multi-file create: (cannot mix with single-create)\n"
	       "    -t|--threadct <nthreads> - Thread count in --multi mode\n"
	       "    -M|--multi <fname>,<size>[,<seed>]\n"
	       "                             - This arg can repeat; will create each fiel\n"
	       "                               if non-zero seed specified, will randomize\n"
	       "\n"
	       "Interleave arguments:\n"
	       "    -N|--nstrips <n>              - Number of strips to use in interleaved allocations.\n"
	       "    -B|--nbuckets <n>             - Number of buckets to divide the device into\n"
	       "                                    (nstrips && nbuckets) causes strided\n"
	       "                                    allocation within a single device.\n"
	       "    -C|--chunksize <size>[kKmMgG] - Size of chunks for interleaved allocation\n"
	       "                                    (default=256M)\n"
	       "\n"
	       "NOTE: the --randomize and --seed arguments are useful for testing; the file is\n"
	       "      randomized based on the seed, making it possible to use the 'famfs verify'\n"
	       "      command later to validate the contents of the file\n"
	       "\n",
	       progname, progname, progname, progname, progname);
}

struct multi_creat {
	char *fname;
	size_t fsize;
	s64 seed;
	int verbose;
	/* outputs */
	int created;
	int rc;
};

static void
free_multi_creat(struct multi_creat *mc, int multi_count)
{
	int i;

	if (!mc)
		return;

	for (i = 0; i < multi_count; i++)
		if (mc[i].fname)
			free(mc[i].fname);

	free(mc);
}

static int
randomize_one(
	const char *filename,
	size_t fsize,
	s64 seed)
{
	size_t fsize_out;
	void *addr;
	char *buf;
	int rc = 0;

	if (!seed)
		return 0;

	addr = famfs_mmap_whole_file(filename, 0, &fsize_out);
	if (!addr) {
		fprintf(stderr, "%s: randomize mmap failed\n",
			__func__);
		return -1;
	}
	if (fsize && fsize != fsize_out) {
		fprintf(stderr, "%s: fsize horky %ld / %ld\n",
			__func__, fsize, fsize_out);
		rc = -1;
		goto out;
	}
	buf = (char *)addr;

	randomize_buffer(buf, fsize_out, seed);
	flush_processor_cache(buf, fsize_out);
	printf("randomized %ld bytes: %s\n", fsize_out, filename);
 out:
	munmap(addr, fsize_out);
	return rc;
}

static int
creat_one(
	const char *filename,
	size_t fsize,
	struct famfs_interleave_param *ip,
	mode_t mode,
	uid_t uid,
	gid_t gid,
	int verbose,
	int *created /* output */)
{
	struct stat st;
	int rc;

	rc = stat(filename, &st);
	if (rc == 0) {
		enum famfs_type ftype;

		if ((st.st_mode & S_IFMT) != S_IFREG) {
			fprintf(stderr, "%s: Error: file %s exists "
				"and is not a regular file\n",
				__func__, filename);
			return -1;
		}

		if ((ftype = file_is_famfs(filename)) == NOT_FAMFS) {
			fprintf(stderr,
				"%s: Error file %s is not in famfs\n",
				__func__, filename);
			return -1;
		}

		/* If the file exists and it's the right size, this
		 * becomes a nop; if the file is the wrong size, it's a fail
		 */
		if (fsize && (size_t)st.st_size != fsize) {
			fprintf(stderr, "%s: Error: file %s exists "
				"and is not the same size\n",
				__func__, filename);
			return -1;
		} else {
			fsize = st.st_size;
		}
		if (verbose)
			printf("%s: re-create (%s) is nop\n",
			       __func__, filename);

		if (created)
			*created = 0;

	} else if (rc < 0) {
		mode_t current_umask;
		int fd;

		if (!fsize) {
			fprintf(stderr, "%s: Error: new file size=zero\n",
				__func__);
			return -1;
		}

		/* This is horky, but OK for the cli */
		current_umask = umask(0022);
		umask(current_umask);
		mode &= ~(current_umask);
		fd = famfs_mkfile(filename, mode, uid, gid, fsize, ip, verbose);
		if (fd < 0) {
			fprintf(stderr, "%s: failed to create file %s\n",
				__func__, filename);
			if (created)
				*created = 0;
			return -1;
		}
		if (created)
			*created = 1;

		close(fd);
	}
	return 0;
}

static int
creat_multi(
	struct multi_creat *mc,
	int multi_count,
	struct famfs_interleave_param *ip,
	mode_t mode,
	uid_t uid,
	gid_t gid,
	int verbose)
{
	int ncreated = 0;
	int errs = 0;
	int i;

	/* Note: create_mutli should not multi-thread, because allocation
	 * needs to be serialized.
	 *
	 * TODO: do multiple creat's under one locked_log, reducing bitmap
	 * generating and allocation overhad
	 */
	for (i = 0; i < multi_count; i++) {
		mc[i].rc = creat_one(mc[i].fname, mc[i].fsize, ip,
				     mode, uid, gid, verbose, &mc[i].created);
	}

	for (i = 0; i < multi_count; i++) {
		if (mc[i].created)
			ncreated++;
		if (mc[i].rc)
			errs ++;
	}

	printf("Create complete for %d of %d files with %d errs\n",
	       ncreated, multi_count, errs);
	return errs;

}

static void
threaded_randomize(void *arg)
{
	struct multi_creat *mc = arg;

	assert(mc);
	mc->rc = randomize_one(mc->fname, mc->fsize, mc->seed);
}


static int
randomize_multi(
	struct multi_creat *mc,
	int multi_count,
	int threadct)
{
	int randomize_ct = 0;
	threadpool thp;
	int errs = 0;
	int i;

	if (threadct < 0 || threadct > 256) {
		fprintf(stderr, "%s: bad threadct: %d\n",
			__func__, threadct);
		return -1;
	}

	printf("%s: randomizing %d files via %d threads\n",
	       __func__, multi_count, threadct);
	if (threadct)
		thp = thpool_init(threadct);
	for (i = 0; i < multi_count; i++) {
		if (threadct)
			thpool_add_work(thp, threaded_randomize,
					(void *)&mc[i]);
		else
			threaded_randomize(&mc[i]);
	}

	if (threadct) {
		thpool_wait(thp);
		famfs_thpool_destroy(thp, 100000 /* 100ms */);
	}

	for (i = 0; i < multi_count; i++) {
		if (mc[i].seed)
			randomize_ct++;
		if (mc[i].rc)
			errs ++;
	}

	printf("Randomize complete for %d of %d files with %d errs\n",
	       randomize_ct, multi_count, errs);
	return errs;

}

int
do_famfs_cli_creat(int argc, char *argv[])
{
	struct famfs_interleave_param interleave_param = { 0 };
	long threadct = sysconf(_SC_NPROCESSORS_ONLN);;
	struct multi_creat *mc = NULL;
	uid_t uid = geteuid();
	gid_t gid = getegid();
	char *filename = NULL;
	int multi_count = 0;
	mode_t mode = 0644;
	int set_stripe = 0;
	int randomize = 0;
	int verbose = 0;
	size_t fsize = 0;
	s64 seed = 0;
	int rc = 0;
	s64 mult;
	int c;

	interleave_param.chunk_size = 0x200000; /* 2MiB default chunk */

	struct option creat_options[] = {
		/* These options set a flag. */
		{"size",        required_argument,             0,  's'},
		{"seed",        required_argument,             0,  'S'},
		{"randomize",   no_argument,                   0,  'r'},
		{"mode",        required_argument,             0,  'm'},
		{"uid",         required_argument,             0,  'u'},
		{"gid",         required_argument,             0,  'g'},
		{"verbose",     no_argument,                   0,  'v'},

		{"multi",       required_argument,             0,  'M'},
		{"threadct",    required_argument,             0,  't'},
		{"chunksize",   required_argument,             0,  'C'},
		{"nstrips",     required_argument,             0,  'N'},
		{"nbuckets",    required_argument,             0,  'B'},
		{0, 0, 0, 0}
	};

	if (threadct < 1)
		threadct = 4;

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+s:S:m:u:g:rC:N:B:M:t:h?v",
				creat_options, &optind)) != EOF) {
		char *endptr;

		switch (c) {

			/* Single-create options */
		case 's':
			fsize = strtoull(optarg, &endptr, 0);
			mult = get_multiplier(endptr);
			if (mult > 0)
				fsize *= mult;
			break;

		case 'S':
			seed = strtoull(optarg, 0, 0);
			break;

		case 'r':
			randomize++;
			break;

			/* General options */
		case 'm':
			mode = strtol(optarg, 0, 8); /* Must be valid octal */
			break;

		case 'u':
			uid = strtol(optarg, 0, 0);
			break;

		case 'g':
			gid = strtol(optarg, 0, 0);
			break;
			/* Interleaving Options */
		case 'C':
			set_stripe++;
			interleave_param.chunk_size = strtoull(optarg,
							       &endptr, 0);
			mult = get_multiplier(endptr);
			if (mult > 0)
				interleave_param.chunk_size *= mult;
			break;

		case 'N':
			set_stripe++;
			interleave_param.nstrips = strtoull(optarg, 0, 0);
			break;

		case 'B':
			set_stripe++;
			interleave_param.nbuckets = strtoull(optarg, 0, 0);
			break;
			/* Multi-creat options */
		case 't':
			threadct = strtoul(optarg, 0, 0);
			break;
		case 'M': {
			char **strings;
			int nstrings;

			if (seed || filename) {
				fprintf(stderr,
					"%s: -S|-f and --multi incompatible\n",
					__func__);
				rc = -1;
				goto multi_err;
			}
			if (!mc)
				mc = calloc(argc, sizeof(*mc));

			strings = tokenize_string(optarg, ",", &nstrings);
			if (!nstrings || nstrings < 2 || nstrings > 3) {
				free_string_list(strings, nstrings);
				fprintf(stderr,
					"%s: bad multi arg(%d): %s "
					"nstrings=%d\n", 
					__func__, multi_count, optarg,
					nstrings);
				rc = -1;
				goto multi_err;
			}

			/* We know nstrings is in the range 2..3 inclusive */
			mc[multi_count].fname = strdup(strings[0]);
			mc[multi_count].fsize = strtoull(strings[1],
							 &endptr, 0);
			/* Apply multiplier, if specified, to the size */
			mult = get_multiplier(endptr);
			if (mult > 0)
				mc[multi_count].fsize *= mult;

			if (nstrings == 3)
				mc[multi_count].seed = strtoull(strings[2],
								0, 0);

			free_string_list(strings, nstrings);

			multi_count++;
			break;
		}

		case 'v':
			verbose++;
			break;

		case 'h':
		case '?':
			famfs_creat_usage(argc, argv);
			return 0;
		}
	}

	if (seed && !randomize) {
		fprintf(stderr,
			"Error seed (-S) without randomize (-r) argument\n");
		return -1;
	}
	if (set_stripe && interleave_param.nstrips > FAMFS_MAX_SIMPLE_EXTENTS) {
		fprintf(stderr,
			"famfs creat error: nstrips(%lld) > %d \n",
			interleave_param.nstrips, FAMFS_MAX_SIMPLE_EXTENTS);
		return -1;
	}

	if (!mc) {
		if (optind > (argc - 1)) {
			fprintf(stderr, "Must specify filename\n");
			return -1;
		}
		filename = argv[optind++];
		rc = creat_one(filename, fsize,
			       (set_stripe) ? & interleave_param : NULL,
			       mode, uid, gid, verbose, NULL);
		if (!rc)
			rc = randomize_one(filename, fsize, seed);
	} else {
		rc = creat_multi(mc, multi_count,
				 (set_stripe) ? & interleave_param : NULL,
				 mode, uid, gid, verbose);
		if (!rc)
			rc = randomize_multi(mc, multi_count, threadct);

	}

multi_err:
	free_multi_creat(mc, multi_count); /* ok if null */
	return rc;
}

/********************************************************************/

void
famfs_mkdir_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs mkdir: Create a directory in a famfs file system:\n"
	       "\n"
	       "    %s mkdir [args] <dirname>\n\n"
	       "\n"
	       "Arguments:\n"
	       "    -h|-?            - Print this message\n"
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
	uid_t uid = geteuid();
	gid_t gid = getegid();
	char *dirpath = NULL;
	mode_t mode = 0755;
	int parents = 0;
	int verbose = 0;
	int c;

	struct option mkdir_options[] = {
		/* These options set a flag. */

		/* These options don't set a flag.
		 * We distinguish them by their indices.
		 */
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
		fprintf(stderr, "%s: Must specify at least one path\n",
			__func__);
		return -1;
	}

	dirpath  = argv[optind++];
	if (parents)
		return famfs_mkdir_parents(dirpath, mode, uid, gid, verbose);

	return famfs_mkdir(dirpath, mode, uid, gid, verbose);
}

/********************************************************************/
void
famfs_verify_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs verify: Verify the contents of a file that was created with 'famfs creat':\n"
	       "    %s verify -S <seed> -f <filename>\n"
	       "\n"
	       "Arguments:\n"
	       "    -h|-?                        - Print this message\n"
	       "    -f|--filename <filename>     - Required file path\n"
	       "    -S|--seed <random-seed>      - Required seed for data verification\n"
	       "    -m|--multi <filename>,<seed> - Verify multiple files in parallel\n"
	       "                                   (specify with multiple instances of this arg)\n"
	       "                                   (cannot combine with separate args)\n"
	       "    -t|--threadct <nthreads>     - Thread count in --multi mode\n"
	       "\n", progname);
}

struct multi_verify {
	char *fname;
	s64 seed;
	int quiet;
	int rc;
};

static void
free_multi_verify(struct multi_verify *mv, int multi_count)
{
	int i;

	if (!mv)
		return;

	for (i = 0; i < multi_count; i++)
		if (mv[i].fname)
			free(mv[i].fname);
	free(mv);
}

static int
verify_one(const char *filename, s64 seed, int quiet)
{
	size_t fsize;
	void *addr;
	char *buf;
	int fd;
	s64 rc;

	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		return 1;
	}
	if (!seed) {
		fprintf(stderr,
			"Must specify random seed to verify file data\n");
		return 1;
	}
	fd = open(filename, O_RDWR, 0);
	if (fd < 0) {
		fprintf(stderr, "open %s failed; fd %d errno %d\n",
			filename, fd, errno);
		return 1;
	}

	addr = famfs_mmap_whole_file(filename, 0, &fsize);
	if (!addr) {
		fprintf(stderr, "%s: randomize mmap failed\n", __func__);
		return 1;
	}
	invalidate_processor_cache(addr, fsize);
	buf = (char *)addr;
	rc = validate_random_buffer(buf, fsize, seed);
	if (rc == -1) {
		if (!quiet)
			printf("Success: verified %ld bytes in file %s\n",
			       fsize, filename);
	} else {
		fprintf(stderr,
			"Verify fail: %s at offset %lld of %ld bytes\n",
			filename, rc, fsize);
		return 1;
	}
	return 0;
}

static void
threaded_verify(void *arg)
{
	struct multi_verify *mv = arg;

	assert(mv);
	mv->rc = verify_one(mv->fname, mv->seed, mv->quiet);
}

static int
verify_multi(
	const struct multi_verify *mv,
	int multi_count,
	int threadct,
	int quiet)
{
	threadpool thp = NULL;
	int errs = 0;
	int i;

	if (threadct < 0 || threadct > 256) {
		fprintf(stderr, "%s: bad threadct: %d\n",
			__func__, threadct);
		return -1;
	}

	if (!quiet)
		printf("%s: threads=%d nfiles=%d\n",
		       __func__, threadct,multi_count);

	if (threadct)
		thp = thpool_init(threadct);
	for (i = 0; i < multi_count; i++) {
		if (threadct)
			thpool_add_work(thp, threaded_verify, (void *)&mv[i]);
		else
			threaded_verify((void *)&mv[i]);
	}

	if (threadct) {
		thpool_wait(thp);
		famfs_thpool_destroy(thp, 100000 /* 100ms */);
	}

	for (i = 0; i < multi_count; i++)
		if (mv[i].rc)
			errs++;

	printf("Verify complete for %d files with %d errs\n",
	       multi_count, errs);
	return errs;
}

int
do_famfs_cli_verify(int argc, char *argv[])
{
	struct multi_verify *mv = NULL;
	char *filename = NULL;
	int multi_count = 0;
	long threadct = sysconf(_SC_NPROCESSORS_ONLN);;
	int quiet = 0;
	s64 seed = 0;
	s64 rc = 0;
	int c;

	struct option verify_options[] = {
		/* These options set a */
		{"seed",        required_argument,             0,  'S'},
		{"filename",    required_argument,             0,  'f'},
		{"multi",       required_argument,             0,  'm'},
		{"threadct",    required_argument,             0,  't'},
		{"quiet",       no_argument,                   0,  'q'},
		{0, 0, 0, 0}
	};

	if (threadct < 1)
		threadct = 4;

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+f:S:m:t:qh?",
				verify_options, &optind)) != EOF) {

		switch (c) {

		case 'S':
			seed = strtoull(optarg, 0, 0);
			if (mv) {
				fprintf(stderr, "%s: -S and -m incompatible\n",
					__func__);
				rc = -1;
				goto multi_err;
			}
			break;

		case 'f':
			filename = optarg;
			if (mv) {
				fprintf(stderr, "%s: -f and -m incompatible\n",
					__func__);
				rc = -1;
				goto multi_err;
			}
			break;
		case 't':
			threadct = strtoul(optarg, 0, 0);
			break;
		case 'm': {
			char **strings;
			int nstrings;

			if (seed || filename) {
				fprintf(stderr,
					"%s: -S|-f and -m incompatible\n",
					__func__);
				rc = -1;
				goto multi_err;
			}
			if (!mv)
				mv = calloc(argc, sizeof(*mv));

			strings = tokenize_string(optarg, ",", &nstrings);
			if (nstrings !=2) {
				free_string_list(strings, nstrings);
				fprintf(stderr,
					"%s: bad multi arg(%d): %s\n",
					__func__, multi_count, optarg);
				rc = -1;
				goto multi_err;
			}
				
			mv[multi_count].fname = strdup(strings[0]);
			mv[multi_count].seed = strtoull(strings[1], 0, 0);
			mv[multi_count].quiet = quiet;
			multi_count++;

			free_string_list(strings, nstrings);
			break;
		}
		case 'q':
			quiet = 1;
			break;

		case 'h':
		case '?':
			famfs_verify_usage(argc, argv);
			return 0;
		}
	}

	if (!mv)
		rc = verify_one(filename, seed, quiet);
	else
		rc = verify_multi(mv, multi_count, threadct, quiet);

multi_err:
	free_multi_verify(mv, multi_count); /* ok if null */
	return rc;

}

/********************************************************************/
void
famfs_flush_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs flush: Flush or invalidate the processor cache for an entire file\n"
	       "\n"
	       "This command is useful for shared memory that is not cache coherent. It should\n"
	       "be called after mutating a file whose mutations need to be visible on other hosts,\n"
	       "and before accessing any file that may have been mutated on other hosts. Note that\n"
	       "logplay also takes care of this, but if the log has not been played since the file\n"
	       "was mutated, this operation may be needed.\n"
	       "\n"
	       "    %s flush [args] <file> [<file> ...]\n"
	       "\n"
	       "Arguments:\n"
	       "    -v           - Verbose output\n"
	       "    -h|-?        - Print this message\n"
	       "\nNOTE: this creates a file system error and is for testing only!!\n"
	       "\n", progname);
}

int
do_famfs_cli_flush(int argc, char *argv[])
{
	char fullpath[PATH_MAX];
	char *file = NULL;
	int verbose = 0;
	int errs = 0;
	int rc;
	int c;

	struct option flush_options[] = {
		/* These options set a */
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+vh?",
				flush_options, &optind)) != EOF) {

		switch (c) {

		case 'v':
			verbose++;
			break;
		case 'h':
		case '?':
			famfs_flush_usage(argc, argv);
			return 0;
		}
	}

	if (optind > (argc - 1)) {
		fprintf(stderr,
			"%s: source and destination filenames required\n",
			__func__);
		famfs_clone_usage(argc, argv);
		return -1;
	}
	while (optind < argc) {
		file = argv[optind++];
		if (realpath(file, fullpath) == NULL) {
			fprintf(stderr, "%s: bad source path %s\n",
				__func__, file);
			errs++;
			continue;
		}

		rc = famfs_flush_file(file, verbose);
		if (rc)
			errs++;
	}
	if (errs)
		printf("%s: %d errors were detected\n", __func__, errs);
	return -errs;
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
	while ((size_t)ctr < len) {
		/*      printf("%8x - ",ptr); */
		for (i = 0; i < 16; i++, ctr++) {
			if ((size_t)ctr >= len)
				break;

			printf("%02x ", adr[ctr]);
		}
		printf("\n");
	}
}


void
famfs_chkread_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

	printf("\n"
	       "famfs chkread: verify that the contents of a file match via read and mmap\n\n"
	       "    %s chkread <famfs-file>\n"
	       "\n"
	       "Arguments:\n"
	       "    -h|-?  - Print this message\n"
	       "    -s     - File is famfs superblock\n"
	       "    -l     - File is famfs log\n"
	       "\n", progname);
}

/**
 * famfs_chkread()
 *
 * This function was added while debugging some dragons in /dev/dax resolution of
 * faults vs. read/write, and it's a useful test. It just verifies that the
 * contents of a file are the same whether accessed by read or mmap
 */
int
do_famfs_cli_chkread(int argc, char *argv[])
{
	int c, fd;
	char *filename = NULL;
	int is_log = 0;
	int is_superblock = 0;
	size_t fsize = 0;
	void *addr;
	char *buf;
	ssize_t rc = 0;
	char *readbuf = NULL;
	struct stat st;

	struct option chkread_options[] = {
		/* These options set a */
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+slh?",
				chkread_options, &optind)) != EOF) {
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
		fprintf(stderr, "%s: Must specify at least one file\n",
			__func__);
		return -1;
	}
	filename = argv[optind++];

	if (filename == NULL) {
		fprintf(stderr, "Must supply filename\n");
		return 1;
	}

	rc = stat(filename, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: could not stat file %s\n",
			__func__, filename);
		return 1;
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
	assert((size_t)rc == fsize);

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
	{"flush",   do_famfs_cli_flush,   famfs_flush_usage},
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
		return 1;
	}

	famfs_log_enable_syslog("famfs", LOG_PID | LOG_CONS, LOG_DAEMON);

	for (i = 0; (famfs_cli_cmds[i].cmd); i++) {
		if (!strcmp(argv[optind], famfs_cli_cmds[i].cmd)) {
			optind++; /* move past cmd on cmdline */
			rc = famfs_cli_cmds[i].run(argc, argv);
			famfs_log_close_syslog();
			return exit_val(rc);
		}
	}

	famfs_log_close_syslog();
	fprintf(stderr, "famfs cli: Unrecognized command %s\n", argv[optind]);
	do_famfs_cli_help(argc, argv);

	return 3;
}

