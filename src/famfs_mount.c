// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <linux/types.h>
#include <stddef.h>
#include <linux/ioctl.h>
#include <libgen.h>
#include <assert.h>
#include <sys/param.h> /* MIN()/MAX() */
#include <sys/file.h>
#include <sys/statfs.h>
#include <sys/mount.h>
#include <sys/mman.h>

#include "famfs_lib_internal.h"

/* XXX TODO:
 * three funcs in this file share code that's very similar:
 * * famfs_get_mpt_by_dev
 * * famfs_path_is_mount_pt
 * * xx
 *
 * Can this be re-factored with less duplication?
 */

#define XLEN 256

/**
 * famfs_get_mpt_by_dev()
 *
 * @mtdev: the primary dax device for a famfs file system.
 *
 * This function determines the mount point by parsing /proc/mounts to
 * find the mount point from a dax device name.
 */
char *
famfs_get_mpt_by_dev(const char *mtdev)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int rc;
	char *answer = NULL;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return NULL;

	while ((read = getline(&line, &len, fp)) != -1) {
		char dev[XLEN];
		char mpt[XLEN];
		char fstype[XLEN];
		char args[XLEN];
		int  x0, x1;
		char *xmpt = NULL;

		if (strstr(line, "famfs")) {
			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, args, &x0, &x1);
			if (rc <= 0)
				goto out;

			xmpt = realpath(mpt, NULL);
			if (!xmpt) {
				fprintf(stderr, "realpath(%s) errno %d\n", mpt, errno);
				continue;
			}
			if (strcmp(dev, mtdev) == 0) {
				/* XXX Should just return xmpt - which is also malloc'd by libc */
				answer = strdup(xmpt);
				free(xmpt);
				free(line);
				fclose(fp);
				return answer;
			}
		}
		if (xmpt)
			free(xmpt);

	}

out:
	fclose(fp);
	if (line)
		free(line);
	return NULL;
}

/**
 * shadow_path_from_opts()
 *
 * Get shadow path from file system mount options (from the opts field in /proc/mounts)
 *
 * @opts             - mount options from a famfs /proc/mounts entry
 * @shadow_path_out  - string to receive shadow path
 * @shadow_path_size - input - size of shadow_path_out string
 *
 * Returns: 0 if shadow path not found
 *          1 if shadow path found (and returned in shadow_path_out)
 */
static int
shadow_path_from_opts(
	const char *opts,
	char *shadow_path_out,
	size_t shadow_path_size)
{
	const char *start;
	const char *end;
	const char *keyword = "shadow=";
	size_t keyword_len = strlen(keyword);

	if (opts == NULL || shadow_path_out == NULL || shadow_path_size == 0) {
		return 0;  /* Invalid opts */
	}

	start = end = opts;

	while (*end != '\0') {
		if (*end == ',' || *(end + 1) == '\0') {
			/* Adjust end if it's the last character */
			if (*(end + 1) == '\0')
				end++;

			/* Check if the segment starts with "shadow=" */
			if ((end - start) >= (long int)keyword_len
			    && strncmp(start, keyword, keyword_len) == 0) {
				const char *value_start = start + keyword_len;
				size_t value_length = end - value_start;

				if (value_length >= shadow_path_size) {
					return 0;  /* output buffer overflow */
				}

				strncpy(shadow_path_out, value_start, value_length);
				shadow_path_out[value_length] = '\0';
				return 1;  /* success */
			}

			/* Move to the next opt */
			start = end + 1;
		}
		end++;
	}

	return 0;  /* No matching argument found */
}

/* XXX: this function should be renamed more descriptively */
/**
 * famfs_path_is_mount_pt()
 *
 * check whether a path is a famfs mount point via /proc/mounts
 *
 * @path:
 * @dev_out: if non-null, the device name will be copied here
 * @shadow_out:
 *
 * Return values
 * 1 - the path is an active famfs mount point
 * 0 - the path is not an active famfs mount point
 */
int
famfs_path_is_mount_pt(
	const char *path,
	char *dev_out,
	char *shadow_out)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int rc;

	if (strlen(path) < 2)
		return 0;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return 0;

	while ((read = getline(&line, &len, fp)) != -1) {
		char dev[XLEN];
		char mpt[XLEN];
		char fstype[XLEN];
		char opts[XLEN] = { 0 };
		char shadow_path[PATH_MAX];
		int  x0, x1;
		char *xmpt = NULL;
		char *xpath = NULL;

		if (!line || strlen(line) < 10) /* No line or too short */
			continue;

		if (strstr(line, "famfs") || strstr(line, "fuse")) { /* lazy test on whole line */
			opts[0] = 0;
			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, opts, &x0, &x1);
			if (rc != 6)
				goto out;

			/* check for famfs in the actual fstype field */
			if (!strstr(fstype, "famfs") && !strstr(opts, "famfs") && !strstr(fstype, "fuse") && !strstr(opts, "shadow")) 
				continue;

			if (strlen(opts) <= strlen("shadow="))
				continue; 

 			xmpt = realpath(mpt, NULL);
			if (!xmpt) {
				fprintf(stderr, "realpath(%s) errno %d\n",
					mpt, errno);
				continue;
			}
			xpath = realpath(path, NULL);
			if (!xpath) {
				fprintf(stderr,
					"input path realpath(%s) errno %d\n",
					path, errno);
				free(xpath);
				free(xmpt);
				continue;
			}
			if (strcmp(xpath, xmpt) != 0) {
				free(xpath);
				free(xmpt);
				continue;
			}

			/* Path matches the mount point of this entry */

			if (shadow_out) {
				rc = shadow_path_from_opts(opts, shadow_path,
							   sizeof(shadow_path));
				if (rc)
					strncpy(shadow_out, shadow_path,
						PATH_MAX - 1);
				else
					shadow_out[0] = 0;
			}

			free(xmpt);
			free(xpath);
			free(line);
			fclose(fp);
			if (dev_out)
				strcpy(dev_out, dev);
			return 1;
		}
		if (xmpt)
			free(xmpt);
		if (xpath)
		  free(xpath);

	}

out:
	fclose(fp);
	if (line)
		free(line);
	return 0;
}

/**
 * Check all famfs-related /proc/mounts entries to see of @shadowpath is already
 * in use.
 *
 * Return 1 if in use, 0 if not
 */
static int
shadow_path_in_use(const char *shadowpath)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int rc;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		return 0;

	while ((read = getline(&line, &len, fp)) != -1) {
		char dev[XLEN];
		char mpt[XLEN];
		char fstype[XLEN];
		char opts[XLEN];
		char entry_shadow[PATH_MAX];
		int  x0, x1;

		if (strstr(line, "famfs")) { /* lazy test on whole line */
			size_t m, n;

			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, opts, &x0, &x1);
			if (rc <= 0)
				continue;

			/* check for famfs in the actual fstype field */
			if (!strstr(fstype, "famfs"))
				continue;

			rc = shadow_path_from_opts(opts, entry_shadow,
						   sizeof(entry_shadow));
			if (!rc)  /* no shadow path in the current entry */
				continue;


			/* We must avoid overlapping shadow paths. This means that
			 * the shorter path is a match for the fist n characters of
			 * the longer path...
			 */
			n = strlen(shadowpath) - 1; /* length, not including NULL term */
			m = strlen(entry_shadow) - 1;
			if (strncmp(shadowpath, entry_shadow, MIN(m, n))) {
				fclose(fp);
				fprintf(stderr, "%s: paths overlap! (%s) (%s)\n",
					__func__, shadowpath, entry_shadow);
				return 1; /* Paths overlap */
			}
		}
	}

	fclose(fp);
	if (line)
		free(line);
	return 0;
}

/**
 * find_mount_point()
 *
 * @path - path to find mount point. @path need not be valid (it might be a
 *         file that is about to be created), but it's parent dir
 *         (i.e. dirname(path) must be valid - and a directory.
 */
char *find_mount_point(const char *path)
{
	struct statfs root_fs, fs, parent_fs;
	struct stat st, parent_st;
	char resolved_path[PATH_MAX];
	char *current_path = NULL;

	/* Get statfs for the root directory to get root fsid */
	if (statfs("/", &root_fs) != 0) {
		perror("statfs for root failed");
		return NULL;
	}

	/* Resolve initial path to absolute or parent if nonexistent */
	if (realpath(path, resolved_path) != NULL) {
		current_path = strdup(resolved_path);
	} else if (errno == ENOENT) {
		/* Path does not exist, check parent directory */
		char temp_path[PATH_MAX];
		strncpy(temp_path, path, PATH_MAX);
		while (realpath(dirname(temp_path), resolved_path) == NULL) {
			/* Continue ascending if parent also doesn't exist */
			strncpy(temp_path, dirname(temp_path), PATH_MAX);
		}
		current_path = strdup(resolved_path);
	} else {
		perror("realpath failed");
		return NULL;
	}

	/* Get initial statfs and stat info */
	if (statfs(current_path, &fs) != 0 || stat(current_path, &st) != 0) {
		perror("statfs or stat failed");
		free(current_path);
		return NULL;
	}

	/* Check if the current path is on the root file system */
	if (fs.f_fsid.__val[0] == root_fs.f_fsid.__val[0] &&
	    fs.f_fsid.__val[1] == root_fs.f_fsid.__val[1]) {
		free(current_path);
		return strdup("/");  // It's the root file system
	}

	/* Ascend the directory tree to find the mount point */
	while (1) {
		char parent_path[PATH_MAX];
		strcpy(parent_path, current_path);

		/* Move one level up in the directory tree */
		if (dirname(parent_path) == NULL) {
			break;
		}

		/* Get statfs and stat info for the parent directory */
		if (statfs(parent_path, &parent_fs) != 0
		    || stat(parent_path, &parent_st) != 0) {
			perror("statfs or stat failed");
			break;
		}

		/* Check if we have reached a different file system */
		if (parent_fs.f_fsid.__val[0] != fs.f_fsid.__val[0] ||
		    parent_fs.f_fsid.__val[1] != fs.f_fsid.__val[1] ||
		    parent_st.st_dev != st.st_dev) {
			break;  // We have found the mount point
		}

		/* Move up if still the same file system */
		free(current_path);
		current_path = realpath(parent_path, NULL);
		if (current_path == NULL) {
			perror("realpath failed");
			return NULL;
		}
	}

	return current_path;  // This is the mount point path
}

static int is_directory(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * get_shadow_root()
 *
 * @shadow_path: a shadow path that should be already valid
 *
 * A valid shadow path should contain a ./root subdirectory,
 * which is the shadow_root path.
 */
char *
famfs_get_shadow_root(
	const char *shadow_path,
	int verbose)
{
	size_t len = strlen(shadow_path);
	const char *suffix = "/root";
	size_t suffix_len = strlen(suffix);
	char *root_path;

	if (!shadow_path)
		return NULL;
	if (!is_directory(shadow_path)) {
		if (verbose)
			fprintf(stderr,
				"%s: shadow_path=%s is not a directory\n",
				__func__, shadow_path);
		return NULL;
	}

	if (len >= suffix_len &&
	    strcmp(shadow_path + len - suffix_len, suffix) == 0) {
		/* Already ends in "/root" */
		if (verbose)
			printf("%s: shadow_path=%s already ends in '/root'\n",
			       __func__, shadow_path);
		return strdup(shadow_path);
	}

	/* Build shadow_path + "/root" */
	root_path = malloc(len + 1 + suffix_len + 1);
	if (!root_path)
		return NULL;

	sprintf(root_path, "%s%s", shadow_path,
		shadow_path[len - 1] == '/' ? "root" : "/root");

	if (is_directory(root_path)) {
		if (verbose)
			printf("%s: shadow_path=%s; "
			       "shadow_path/root exists Yay\n",
			       __func__, shadow_path);
			       
		return root_path;
	}

	fprintf(stderr,
		"%s: shadow_path=%s exists, but not shadow_path/root\n",
		__func__, shadow_path);
	free(root_path);
	return NULL;
}



/*
 * return true if valid, false if not
 */
int shadow_path_valid(const char *path)
{
	char *path_copy, *parent;
	struct stat statbuf;

	/* Check for NULL or empty path */
	if (path == NULL || *path == '\0')
		return 0;

	/* Check if the path already exists */
	if (access(path, F_OK) != 0)
		return 0;

	/* Duplicate path to safely use dirname() */
	path_copy = strdup(path);
	parent = dirname(path_copy); /* Extract parent directory */

	/* Check if parent exists and is a directory */
	if (stat(parent, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
		free(path_copy);
		return 1;
	}

	/* Parent does not exist or is not a directory */
	free(path_copy);
	return 0;
}

#define NARGV 64

static int
famfs_start_fuse_daemon(
	const char *mpt,
	const char *daxdev,
	const char *shadow,
	ssize_t timeout,
	int useraccess,
	int default_perm,
	int debug,
	int verbose)
{
	char target_path[PATH_MAX] = { 0 };
	char exe_path[PATH_MAX] = { 0 };
	char opts[PATH_MAX] = { 0 };
	char *argv[NARGV] = { 0 };
	char asan_log_path[PATH_MAX];
	int argc = 0;
	ssize_t len;
	char *dir;
	pid_t pid;

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "%s: failed to fork\n", __func__);
		return -1;
	}
	  
	if (pid > 0) {
		famfs_log(FAMFS_LOG_DEBUG, "%s: pid=%d\n", __func__, pid);
		if (debug)
			printf("%s: pid=%d\n", __func__, pid);

		return 0;
	}

	len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
	if (len < 0)
		fprintf(stderr, "%s: readlink /proc/self/exe failed\n",
			__func__);
	dir = dirname(exe_path);
	snprintf(target_path, sizeof(target_path) - 1, "%s/%s",
		 dir, "famfs_fused");

	/*
	 * Set addr sanitizer env option to capture any any sanitizer errors
	 * into a log file. This is needed as famfs_fused becomes a daemon
	 * and loses access to stderr, this creates a log in shadow dir if
	 * asan related errors are encountered.
	 */
	snprintf(asan_log_path, sizeof(asan_log_path),
		 "log_path=%s/asan_famfs_fused.log", shadow);
	setenv("ASAN_OPTIONS", asan_log_path, 1);

	/* fsname=/dev/dax1.0 sets the string in column 1 of /proc/mounts */
	snprintf(opts, sizeof(opts),
		 "daxdev=%s,shadow=%s,fsname=%s",
		 daxdev, shadow, daxdev);
	if (timeout >= 0) {
		char timeout_arg[PATH_MAX];

		snprintf(timeout_arg, sizeof(timeout_arg),
			 ",timeout=%f", (float)timeout);
		strncat(opts, timeout_arg,
			sizeof(opts) - strlen(opts) - 1);
	}

	if (useraccess) {
		char useraccess_arg[PATH_MAX] = ",allow_other";
		strncat(opts, useraccess_arg,
			sizeof(opts) - strlen(opts) - 1);
	}
	
	if (default_perm) {
		char useraccess_arg[PATH_MAX] = ",default_permissions";
		strncat(opts, useraccess_arg,
			sizeof(opts) - strlen(opts) - 1);
	}

	if (verbose)
		printf("%s: opts: %s\n", __func__, opts);

	argv[argc++] = strdup(daxdev);
	if (debug) {
		argv[argc++] = "-s"; /* single-threaded */
		argv[argc++] = "-d"; /* debug (and no daemonize) */
	}
	argv[argc++] = "-o";
	argv[argc++] = strdup(opts);
	argv[argc++] = strdup(mpt);
	argv[argc++] = NULL;

	assert (argc < NARGV);

	execv(target_path, argv);

	return 0;
}

static char *
gen_shadow_dir(void)
{
	char template[] = "/tmp/famfs_shadow_XXXXXX"; /* Must end with XXXXXX */
	char *shadow = mkdtemp(template);  /* Creates a dir with unique name */

	if (!shadow || shadow[0] == '\0') {
		fprintf(stderr,
			"%s: Err %d failed to generate shadow path (%s)\n",
			__func__, errno, template);
		return NULL;
	}

	return strdup(shadow );
}

static char *
gen_dummy_mpt(void)
{
	char template[] = "/tmp/famfs_dummy_XXXXXX";  /* Must end with XXXXXX */
	char *shadow = mkdtemp(template); /* Creates a dir with unique name */

	if (!shadow || shadow[0] == '\0') {
		fprintf(stderr,
			"%s: Err %d failed to generate shadow path (%s)\n",
			__func__, errno, template);
		return NULL;
	}

	return strdup(shadow );
}

/**
 * famfs_mount_fuse() - mount a famfs file system via fuse
 *
 * @realdaxdev
 * @realmpt
 * @realshadow
 * @timeout
 * @logplay_use_mmap
 * @useraccess
 * @default_perm
 * @bounce_dax       - Disable and re-enable daxdev before proceeding with mount
 * @dummy            - Perform a mount and create meta files but don't verify
 *                     superblock and log, and don't play the log.
 * @dummy_log_size   - Size of log file for dummy mount
 * @debug
 * @verbose
 */
int
famfs_mount_fuse(
	const char *realdaxdev,
	const char *realmpt,
	const char *realshadow,
	ssize_t timeout,
	int logplay_use_mmap,
	int useraccess,
	int default_perm,
	int bounce_dax,
	int dummy,
	u64 dummy_log_size,
	int debug,
	int verbose)
{
	u64 log_offset = FAMFS_SUPERBLOCK_SIZE;
	char superblock_path[PATH_MAX] = {0};
	struct famfs_superblock *sb = NULL;
	char shadow_root[PATH_MAX] = {0};
	u64 log_size = dummy_log_size;
	size_t sb_size, log_size_out;
	enum famfs_system_role role;
	int shadow_created = 0;
	char *local_shadow;
	char *mpt_check;
	int mounted = 0;
	int umountrc;
	int rc = 0;

	mpt_check = famfs_get_mpt_by_dev(realdaxdev);
	if (mpt_check) {
		fprintf(stderr, "%s: cannot mount while %s is mounted on %s\n",
				__func__, realdaxdev, mpt_check);
		free(mpt_check);
		return -1;
	}

	if (realshadow) {
		local_shadow = strdup(realshadow);
		shadow_created = 1;
	}
	else {
		local_shadow = gen_shadow_dir();
		if (local_shadow)
			shadow_created = 1;
		if (useraccess) {
			rc = chmod(local_shadow, 0755);
			if (rc) {
				fprintf(stderr, "%s: failed to chmod shadow path %s\n",
					__func__, local_shadow);
				rc = -1;
				goto out;
			}
		}
	}

	if (shadow_path_in_use(local_shadow)) {
		fprintf(stderr, "%s: shadow path is already in use!\n",
			__func__);
		rc = -EALREADY;
		goto out;
	}

	/* Skip validating (and creating) shadow dir if we just created it */
	if (!shadow_created) {
		if (!shadow_path_valid(local_shadow)) {
			fprintf(stderr, "%s: invalid shadow path (%s)\n",
				__func__, local_shadow);
			rc = -1;
			goto out;
		}

		rc = mkdir(local_shadow, 0755);
		if (rc) {
			fprintf(stderr, "%s: failed to create shadow path %s\n",
				__func__, local_shadow);
			rc = -1;
			goto out;
		}
	}

	/* The official "shadow_path" is in local_shadow[], but the root of the
	 * shadow yaml tree is now <local_shadow>/root, allowing config &
	 * control files to live at <local shadow> without being exposed.
	 */
	snprintf(shadow_root, sizeof(shadow_root), "%s/root", local_shadow);
	rc = mkdir(shadow_root, 0755);
	if (rc) {
		fprintf(stderr,
			"%s: failed to create shadow root path %s\n",
			__func__, local_shadow);
		rc = -1;
		goto out;
	}

	if (bounce_dax) {
		/* Not more access allowed to the raw daxdev after mkmeta! */
		rc = famfs_bounce_daxdev(realdaxdev, verbose);
		if (rc) {
			fprintf(stderr, "%s: failed to bounce daxdev %s\n",
				__func__, realdaxdev);
			return rc;
		}
	}

	/* Start the fuse daemon, which mounts the FS */
	rc = famfs_start_fuse_daemon(realmpt, realdaxdev, local_shadow, timeout,
				     useraccess, default_perm, debug, verbose);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to start fuse daemon\n", __func__);
		return rc;
	}
	mounted = 1;

	/* Create the superblock shadow
	 * This can be created with no info from the media because the superblock
	 * is always 2MiB at offset 0 of the primary daxdev
	 */
	rc = __famfs_mkmeta_superblock(shadow_root, 1 /* shadow */, verbose);
	if (rc) {
		fprintf(stderr, "%s: failed to create superblock file\n",
			__func__);
		goto out;
	}

	/* Verify that the superblock meta file has appeared
	 * (i.e. the initial fuse mount was successful, and our fuse server
	 * (famfs_fused) has discovered the superblock meta file)
	 */
	if (check_file_exists(realmpt, ".meta/.superblock",
			      1000 /* timeout */,
			      FAMFS_SUPERBLOCK_SIZE, &sb_size, verbose + 1)) {
		fprintf(stderr, "%s: superblock file failed to appear\n",
			__func__);
		rc = -1;
		goto out;
	}

	/* Sanity-check and then mmap the superblock via its file */
	if (sb_size != FAMFS_SUPERBLOCK_SIZE) {
		fprintf(stderr, "%s: bad superblock size=%ld\n",
			__func__, sb_size);
		rc = -1;
		goto out;
	}

	snprintf(superblock_path, PATH_MAX - 1, "%s/%s",
		 realmpt, ".meta/.superblock");
	sb = (struct famfs_superblock *)famfs_mmap_whole_file(
		superblock_path, 1 /* read_only */,
		&sb_size);
	if (!sb) {
		fprintf(stderr, "%s: failed to mmap superblock file\n",
			__func__);
		rc = -1;
		goto out;
	}

	/* Get the role, log offset and size via the superblock
	 * meta file
	 */
	role = __famfs_get_role_and_logstats(sb, &log_offset, &log_size);
	if (!dummy) {
		switch (role) {
		case FAMFS_NOSUPER:
			/* FAMFS_NOSUPER is the only case where we abort a
			 * non-dummy mount: unmap before umount */
			sb = NULL;
			rc = munmap(sb, FAMFS_SUPERBLOCK_SIZE);
			if (rc)
				fprintf(stderr, "%s: failed to munmap superblock"
					" errno=%d\n", __func__, errno);
			rc = -EPERM;
			goto out;
			break;
		default:
		}
	}

	if (role == FAMFS_MASTER || role == FAMFS_CLIENT)
		assert(sb->ts_log_offset == FAMFS_SUPERBLOCK_SIZE);

	if (log_size > 0) {
		/* Now that we know the offset and size of the log file, create
		 * its shadow meta file
		 */
		rc = __famfs_mkmeta_log(shadow_root, log_offset, log_size,
					role, 1 /* shadow */, verbose);
		if (rc) {
			fprintf(stderr, "%s: failed to create superblock file\n",
				__func__);
			goto out;
		}
	
		/* Wait for the log file to appear before playing the log
		 */
		if (check_file_exists(realmpt, ".meta/.log",
				      1000 /* timeout */,
				      log_size, &log_size_out, verbose)) {
			fprintf(stderr, "%s: superblock file failed to appear\n",
				__func__);
			rc = -1;
			goto out;
		}

		assert(log_size == log_size_out);
	}

	/* Unmap the superblock, though logplay will re-map it */
	if (sb) {
		int rc2 = munmap(sb, FAMFS_SUPERBLOCK_SIZE);
		if (rc2)
			fprintf(stderr, "%s: failed to unmap superblock\n",
				__func__);

		sb = NULL;
	}

	if (!dummy) {
		/* Finally, play the log */
		rc = famfs_logplay(realmpt, logplay_use_mmap,
				   0 /* dry_run */,
				   0 /* client_mode */,
				   local_shadow,
				   0 /* shadow_test */,
				   verbose);
		if (rc < 0) {
			fprintf(stderr, "%s: failed to play the log\n",
				__func__);
			goto out;
		}
	}
out:
	if (sb) {
		int rc2 = munmap(sb, FAMFS_SUPERBLOCK_SIZE);
		if (rc2)
			fprintf(stderr, "%s: failed to munmap superblock\n",
				__func__);
	}
	if (rc && mounted) {
		fprintf(stderr, "%s: unmounting due to error\n", __func__);
		umountrc = umount(realmpt);
		if (umountrc)
			fprintf(stderr,
				"%s: umount failed for %s (rc=%d errno=%d)\n",
				__func__, realmpt, umountrc, errno);
	}

	free(local_shadow);
	return rc;
}

int
famfs_dummy_mount(
	const char *realdaxdev,
	size_t log_size,
	char **mpt_out,
	int debug,
	int verbose)
{
	char *mpt = gen_dummy_mpt();
	size_t size;
	int rc;

	rc = famfs_get_device_size(realdaxdev, &size, 1 /* only char daxdevs */);
	if (rc) {
		fprintf(stderr, "%s: bad daxdev %s\n", __func__, realdaxdev);
		return rc;
	}

	assert(mpt_out);
	rc = famfs_mount_fuse(realdaxdev, mpt, NULL, 100, 0,
			      1 /* useraccess */,
			      1 /* default_perm */,
			      0 /* bounce_dax */,
			      1 /* dummy */,
			      log_size,
			      debug, verbose);
	if (rc) {
		fprintf(stderr, "%s: dummy mount failed for %s at %s\n",
			__func__, realdaxdev, mpt);
		famfs_log(FAMFS_LOG_ERR,
			  "%s: dummy mount failed for %s at %s\n",
			__func__, realdaxdev, mpt);
		free(mpt);
		return rc;
	}

	*mpt_out = mpt; /* Caller must free mpt */
	return 0;
}
