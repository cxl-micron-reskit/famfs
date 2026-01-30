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
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <linux/types.h>
#include <stddef.h>
#include <sys/mman.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */
#include <libgen.h>
#include <assert.h>
#include <sys/param.h> /* MIN()/MAX() */
#include <zlib.h>
#include <sys/file.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <sys/utsname.h>
#include <curl/curl.h>

#include <linux/famfs_ioctl.h>

#include "famfs_meta.h"
#include "famfs_lib.h"
#include "famfs_lib_internal.h"
#include "bitmap.h"
#include "mu_mem.h"
#include "thpool.h"

extern int mock_uuid;
/**
 * get_multiplier()
 *
 * For parsing numbers on command lines with K/M/G for KiB etc.
 */
s64 get_multiplier(const char *endptr)
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
	case 0: /* null terminator */
		return 1;
	}
	++endptr;
	if (*endptr) /* If the unit was not the last char in string, it's an error */
		return -1;
	return multiplier;
}

void
famfs_dump_logentry(
	const struct famfs_log_entry *le,
	const int index,
	const char *prefix,
	int verbose)
{
	u32 i;

	if (!verbose)
		return;

	switch (le->famfs_log_entry_type) {
	case FAMFS_LOG_FILE: {
		const struct famfs_log_file_meta *fm = &le->famfs_fm;
		const struct famfs_log_fmap *fmap = &fm->fm_fmap;

		printf("%s: %d file=%s size=%lld\n", prefix, index,
		       fm->fm_relpath, fm->fm_size);

		switch (fmap->fmap_ext_type) {
		case FAMFS_EXT_SIMPLE:
			if (verbose > 1) {
				printf("\tFAMFS_EXT_SIMPLE:\n");
				for (i = 0; i < fmap->fmap_nextents; i++)
					printf("\text: %d tofs=0x%llx len=0x%llx\n",
					       i, fmap->se[i].se_offset, fmap->se[i].se_len);
			}
			break;

		case FAMFS_EXT_INTERLEAVE: {
			u64 j;

			printf("\tniext=%d\n", fmap->fmap_niext);
			for (i = 0; i < fmap->fmap_niext; i++) {
				const struct famfs_simple_extent *strips = fmap->ie[i].ie_strips;
				u64 nstrips = fmap->ie[i].ie_nstrips;

				for (j = 0; j < nstrips; j++)
					printf("\t\tstrip: dev=%lld ofs=0x%llx len=0x%llx\n",
					       strips[j].se_devindex, strips[j].se_offset,
					       strips[j].se_len);
			}
			break;
		}
		default:
			printf("\tError unrecognized extent type\n");
		}
		break;
	}

	case FAMFS_LOG_MKDIR: {
		const struct famfs_log_mkdir *md = &le->famfs_md;
		printf("%s: mkdir: %o %d:%d: %s \n", prefix,
		       md->md_mode, md->md_uid, md->md_gid, md->md_relpath);
		break;
	}

	case FAMFS_LOG_DELETE:
	default:
		printf("\tError unrecognized log entry type\n");
	}
}

void famfs_dump_super(struct famfs_superblock *sb)
{
	int rc;

	assert(sb);
	rc = famfs_check_super(sb, NULL, NULL);
	if (rc)
		fprintf(stderr, "invalid superblock\n");

	printf("famfs superblock:\n");
	printf("\tmagic:       %llx\n", sb->ts_magic);
	printf("\tversion:     %lld\n", sb->ts_version);
	printf("\tlog offset:  %lld\n", sb->ts_log_offset);
	printf("\tlog len:     %lld\n", sb->ts_log_len);
}

void famfs_dump_log(struct famfs_log *logp)
{
	int rc;

	assert(logp);
	rc = famfs_validate_log_header(logp);
	if (rc)
		fprintf(stderr, "Error invalid log header\n");

	printf("famfs log: (%p)\n", logp);
	printf("\tmagic:      %llx\n", logp->famfs_log_magic);
	printf("\tlen:        %lld\n", logp->famfs_log_len);
	printf("\tlast index: %lld\n", logp->famfs_log_last_index);
	printf("\tnext index: %lld\n", logp->famfs_log_next_index);
}

#define SYS_UUID_DIR "/opt/famfs"
#define SYS_UUID_FILE "system_uuid"

int
famfs_get_system_uuid(uuid_le *uuid_out)
{
	FILE *f;
	char uuid_str[48];  /* UUIDs are 36 characters long, plus null terminator */
	uuid_t uuid;
	char *sys_uuid_dir = SYS_UUID_DIR;
	char sys_uuid_file_path[PATH_MAX] = {0};

	if (mock_uuid)
		sys_uuid_dir = "/tmp";

	snprintf(sys_uuid_file_path, PATH_MAX - 1, "%s/%s",
			sys_uuid_dir, SYS_UUID_FILE);

	/* Create system uuid file if it's missing */
	if (famfs_create_sys_uuid_file(sys_uuid_file_path) < 0) {
		fprintf(stderr, "Failed to create system-uuid file\n");
		return -1;
	}
	f = fopen(sys_uuid_file_path, "r");
	if (f == NULL) {
		fprintf(stderr, "%s: unable to open system uuid at %s\n",
				__func__, sys_uuid_file_path);
		return -errno;
	}

	/* gpt */
	if (fscanf(f, "%36s", uuid_str) != 1 || mock_uuid) {
		fprintf(stderr, "%s: unable to read system uuid at %s, errno: %d\n",
				__func__, sys_uuid_file_path, errno);
		fclose(f);
		/* Remove system uuid file if it's not a valid uuid */
		unlink(sys_uuid_file_path);
		return -1;
	}

	fclose(f);

	if (uuid_parse(uuid_str, uuid) == -1) {
		/* If this fails, we should check for a famfs-specific UUID file - and if
		 * that doesn't already exist we should generate the UUID and write the file
		 */
		fprintf(stderr, "%s: Error parsing UUID (%s)\n", __func__, uuid_str);
		return -EINVAL;
	}
	memcpy(uuid_out, uuid, sizeof(uuid));
	return 0;
}

void
famfs_uuidgen(uuid_le *uuid)
{
	uuid_t local_uuid;

	uuid_generate(local_uuid);
	memcpy(uuid, &local_uuid, sizeof(local_uuid));
}

void
famfs_print_uuid(const uuid_le *uuid)
{
	uuid_t local_uuid;
	char uuid_str[37];

	memcpy(&local_uuid, uuid, sizeof(local_uuid));
	uuid_unparse(local_uuid, uuid_str);

	printf("%s\n", uuid_str);
}

/*
 * Check if uuid file exists, if not, create it
 * and update it with a new uuid.
 *
 */
int famfs_create_sys_uuid_file(char *sys_uuid_file)
{
	int uuid_fd, rc;
	char uuid_str[37];  /* UUIDs are 36 char long, plus null terminator */
	uuid_t local_uuid;
	struct stat st = {0};
	uuid_le sys_uuid;

	/* Do nothing if file is present */
	rc = stat(sys_uuid_file, &st);
	if (rc == 0 && (st.st_mode & S_IFMT) == S_IFREG)
		return 0;

	/* File not found, check for directory */
	rc = stat(SYS_UUID_DIR, &st);
	if (rc < 0 && errno == ENOENT) {
		/* No directory found, create one */
		rc = mkdir(SYS_UUID_DIR, 0755);
		if (rc || mock_uuid) {
			fprintf(stderr, "%s: error creating dir %s errno: %d\n",
				__func__, SYS_UUID_DIR, errno);
			return -1;
		}
	}

	uuid_fd = open(sys_uuid_file, O_RDWR | O_CREAT, 0444);
	if (uuid_fd < 0) {
		fprintf(stderr, "%s: failed to open/create %s errno %d.\n",
			__func__, sys_uuid_file, errno);
		return -1;
	}

	famfs_uuidgen(&sys_uuid);
	memcpy(&local_uuid, &sys_uuid, sizeof(sys_uuid));
	uuid_unparse(local_uuid, uuid_str);
	rc = write(uuid_fd, uuid_str, 37);
	if (rc < 0 || mock_uuid) {
		fprintf(stderr, "%s: failed to write uuid to %s, errno: %d\n",
			__func__, sys_uuid_file, errno);
		unlink(sys_uuid_file);
		return -1;
	}
	return 0;
}

int
famfs_flush_file(const char *filename, int verbose)
{
	struct stat st;
	size_t size;
	void *addr;
	int rc;

	rc = stat(filename, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: file not found (%s)\n", __func__, filename);
		return 3;
	}
	if ((st.st_mode & S_IFMT) != S_IFREG) {
		if (verbose)
			fprintf(stderr, "%s: not a regular file: (%s)\n",
				__func__, filename);
		return 2;
	}

	/* Only flush regular files */

	addr = famfs_mmap_whole_file(filename, 1, &size);
	if (!addr)
		return 1;

	if (verbose > 1)
		printf("%s: flushing: %s\n", __func__, filename);

	/* We don't know caller needs a flush or an invalidate, so barriers on both sides */
	hard_flush_processor_cache(addr, size);
	return 0;
}

int
kernel_symbol_exists(
	const char *symbol_name,
	const char *mod_name,
	const int verbose)
{
	FILE *fp;
	char line[PAGE_SIZE];
	int symbol_len;

	assert(symbol_name);
	assert(mod_name);

	symbol_len = strlen(symbol_name);

	if (verbose)
		printf("%s: looking for function %s in module [%s]\n",
		       __func__, symbol_name, mod_name);

	fp = fopen("/proc/kallsyms", "r");
	if (!fp) {
		perror("Failed to open /proc/kallsyms (are you root?)");
		return 0;
	}

	while (fgets(line, sizeof(line), fp)) {
		char addr[32], type, name[256], mname[256];
		int rc;

		/* Fast check that both strings are in the line */
		if (!strstr(line, mod_name) || !strstr(line, symbol_name))
			continue;

		if (verbose > 1)
			printf("%s: candidate line: %s", __func__, line);

		/* Each line is like: "ffffffffa0002000 T startup_64 [module_name]" */
		rc = sscanf(line, "%31s %c %255s [%[^]]]", addr, &type, name, mname); 
		if (rc == 4) {
			if (verbose > 1)
				printf("(symbol=%s module=%s)", name, mname);
			if ((strncmp(name, symbol_name, symbol_len) == 0)
			    && (strcmp(mname, mod_name) == 0)) {
				fclose(fp);
				if (verbose)
					printf("...MATCH\n");
				return 1;
			}
		}
		else
			printf("(sscanf returned %d)", rc); 
		if (verbose > 1)
			printf("\n");
	}

	fclose(fp);
	return 0;
}

/**
 * famfs_get_kernel_type()
 *
 * Return a valid kernel type (FAMFS_FUSE or FAMFS_V1) that matches the
 * running kernel, or NOT_FAMFS if the running kernel has support for neither.
 */
enum famfs_type
famfs_get_kernel_type(int verbose)
{
	/* First choice is fuse */
	if (kernel_symbol_exists("famfs_fuse_iomap_begin", "fuse", verbose))
		return FAMFS_FUSE;

	if (kernel_symbol_exists("famfs_create", "famfsv1", verbose))
		return FAMFS_V1;

	if (kernel_symbol_exists("famfs_create", "famfs", verbose))
		return FAMFS_V1;

	if (verbose)
		fprintf(stderr, "%s: no famfs symbols in running kernel\n",
			__func__);

	return NOT_FAMFS;
}

/**
 * famfs_get_kernel_version() - Get the running kernel's major and minor version
 * @major: Output pointer for major version number
 * @minor: Output pointer for minor version number
 *
 * Returns 0 on success, -1 on failure.
 */
static int famfs_get_kernel_version(int *major, int *minor)
{
	struct utsname uts;

	if (uname(&uts) < 0)
		return -1;

	if (sscanf(uts.release, "%d.%d", major, minor) != 2)
		return -1;

	return 0;
}

/**
 * famfs_daxmode_required() - Check if kernel requires explicit dax mode setting
 *
 * Starting with kernel 6.15, famfs requires explicit binding to the fsdev_dax
 * driver instead of device_dax.
 *
 * Returns true if kernel version >= 6.15, false otherwise.
 */
bool famfs_daxmode_required(void)
{
	int major, minor;

	if (famfs_get_kernel_version(&major, &minor) < 0)
		return false; /* On error, assume not required */

	if (major > 6)
		return true;
	if (major == 6 && minor >= 15)
		return true;

	return false;
}

/**
 * check_file_exists()
 *
 * Check if file at basepath/relpath exists within timeout seconds
 * This is primarily used during fuse mount, to wait for the superblock
 * file to become visible via the fuse mount.
 *
 * Returns 0 if the file is found before timeout.
 * Returns -1 if the timeout is reached and file is still not found.
 */
int check_file_exists(
	const char *basepath,
	const char *relpath,
	int timeout,
	size_t expected_size,
	size_t *size_out,
	int verbose)
{
	char fullpath[4096];
	struct stat st;
	useconds_t wait_us = 100 * 1000; /* 100ms */
	useconds_t waited_us = 0;
	useconds_t timeout_us = timeout * 1000 * 1000;

	/* Build full path */
	snprintf(fullpath, sizeof(fullpath), "%s/%s", basepath, relpath);

	if (verbose)
		printf("%s: checking for path: %s\n", __func__, fullpath);

	/* Loop until timeout */
	while (waited_us < timeout_us) {
		int fd = open(fullpath, O_RDONLY);
		if (fd > 0) {
			if (fstat(fd, &st) == 0) {
				/* File exists */
				if (verbose)
					printf("%s: waited %dms\n", __func__,
					       waited_us / 1000);
				if (expected_size
				    && (size_t)st.st_size != expected_size) {
					close(fd);
					fprintf(stderr,
					   "%s: bad size %ld != %ld, retry\n",
						__func__, st.st_size,
						expected_size);
					goto retry;
				}
				if (verbose)
					printf("%s: good size\n", __func__);

				if (size_out)
					*size_out = st.st_size;

				close(fd);
				return 0;
			}
		}
	retry:
		usleep(wait_us);
		waited_us += wait_us;
	}

	/* File did not appear within timeout */
	return -1;
}

int count_open_fds(void)
{
	int count = 0;
	DIR *dir = opendir("/proc/self/fd");

	if (dir == NULL) {
		perror("opendir");
		return -1;
	}

	while (readdir(dir) != NULL)
		count++;

	closedir(dir);

	/* Subtract 2 for "." and ".." entries */
	return count - 2;
}

void free_string_list(char **strings, int nstrings)
{
	int i;

	if (!strings)
		return;

	for (i = 0; i < nstrings; i++)
		if (strings[i])
			free(strings[i]);

	free(strings);
}

/*
 * Splits a delimited string (no whitespace) into an array of strings.
 * - input: input string to split
 * - delimiter: single character delimiter
 * - out_count: receives the number of tokens
 * Returns: array of strings (char **), or NULL on failure.
 * Caller must free each string and the array itself.
 */
char **tokenize_string(const char *input, char delimiter, int *out_count)
{
	char delim_str[2] = {delimiter, '\0'};
	char *copy;
	char *token;
	char **result;
	const char *p;
	int count;
	int i = 0;
	int j;

	if (input == NULL || out_count == NULL)
		return NULL;

	copy = strdup(input);
	if (copy == NULL)
		return NULL;

	/* count delimiters to determine array size */
	count = 1;
	for (p = input; *p != '\0'; ++p) {
		if (*p == delimiter)
			count++;
	}

	result = (char **) malloc(count * sizeof(char *));
	if (result == NULL) {
		free(copy);
		return NULL;
	}

	token = strtok(copy, delim_str);
	while (token != NULL && i < count) {
		result[i] = strdup(token);
		if (result[i] == NULL) {
			for (j = 0; j < i; j++)
				free(result[j]);
			free(result);
			free(copy);
			return NULL;
		}
		i++;
		token = strtok(NULL, delim_str);
	}

	free(copy);
	*out_count = i;
	return result;
}

void famfs_thpool_destroy(threadpool thp, useconds_t sleep_us)
{
	thpool_destroy(thp);
	usleep(sleep_us);
}

void log_file_mode(
	const char *caller,
	const char *name,
	const struct stat *st,
	int log_level)
{
	struct passwd *pw = getpwuid(st->st_uid);
	struct group  *gr = getgrgid(st->st_gid);
	char perms[11] = "----------";
	mode_t mode = st->st_mode;
	char timebuf[64];

	if (S_ISDIR(mode))  perms[0] = 'd';
	else if (S_ISLNK(mode)) perms[0] = 'l';
	else if (S_ISCHR(mode)) perms[0] = 'c';
	else if (S_ISBLK(mode)) perms[0] = 'b';
	else if (S_ISFIFO(mode)) perms[0] = 'p';
	else if (S_ISSOCK(mode)) perms[0] = 's';

	if (mode & S_IRUSR) perms[1] = 'r';
	if (mode & S_IWUSR) perms[2] = 'w';
	if (mode & S_IXUSR) perms[3] = 'x';
	if (mode & S_IRGRP) perms[4] = 'r';
	if (mode & S_IWGRP) perms[5] = 'w';
	if (mode & S_IXGRP) perms[6] = 'x';
	if (mode & S_IROTH) perms[7] = 'r';
	if (mode & S_IWOTH) perms[8] = 'w';
	if (mode & S_IXOTH) perms[9] = 'x';

	/* Handle suid, sgid, sticky bits */
	if (mode & S_ISUID) perms[3] = (perms[3] == 'x') ? 's' : 'S';
	if (mode & S_ISGID) perms[6] = (perms[6] == 'x') ? 's' : 'S';
	if (mode & S_ISVTX) perms[9] = (perms[9] == 'x') ? 't' : 'T';

	strftime(timebuf, sizeof(timebuf), "%b %e %H:%M",
		 localtime(&st->st_mtime));

	famfs_log(log_level, "%s: %s %2lu %-8s %-8s %8lld %s %s\n",
		  caller, perms,
		  (unsigned long)st->st_nlink,
		  pw ? pw->pw_name : "?",
		  gr ? gr->gr_name : "?",
		  (long long)st->st_size,
		  timebuf,
		  name);

}

static sigjmp_buf jmp_env;

/* Async-signal-safe handler */
static void sigbus_handler(int sig)
{
	(void)sig;
	siglongjmp(jmp_env, 1);
}

bool ptr_is_readable(const void *p)
{
	struct sigaction sa_old, sa_new;
	bool result;

	/* Install temporary SIGBUS handler */
	memset(&sa_new, 0, sizeof(sa_new));
	sa_new.sa_handler = sigbus_handler;
	sigemptyset(&sa_new.sa_mask);
	sa_new.sa_flags = SA_NODEFER;   /* don't block SIGBUS while in handler */

	if (sigaction(SIGBUS, &sa_new, &sa_old) < 0) {
		/* If we can't install the handler, safest answer is "bad" */
		return false;
	}

	if (sigsetjmp(jmp_env, 1) == 0) {
		/*
		 * Attempt to read from the pointer.
		 * Use volatile so compiler cannot optimize away.
		 */
		volatile uint8_t tmp = *(volatile const uint8_t *)p;
		(void)tmp;

		result = true;
	} else {
		/* We jumped here from the SIGBUS handler */
		result = false;
	}

	/* Restore old handler */
	sigaction(SIGBUS, &sa_old, NULL);

	return result;
}

int exit_val(int rc) {
	/* Take absolute value */
	int val = (rc < 0) ? -rc : rc;

	/* If less than 127, return it; otherwise cap at 127 */
	if (val < 127) {
		return val;
	} else {
		return 127;
	}
}

void *
famfs_read_fd_to_buf(int fd, ssize_t max_size, ssize_t *size_out)
{
	char *buf;
	ssize_t n;

	if (max_size > FAMFS_YAML_MAX)
		famfs_log(FAMFS_LOG_ERR, "%s: max_size=%lld > limit=%d\n",
			 __func__, max_size, FAMFS_YAML_MAX);

	buf = calloc(1, max_size + 8);
	if (!buf) {
		famfs_log(FAMFS_LOG_ERR, "%s: failed to malloc(%ld)\n",
			 __func__, max_size);
		return NULL;
	}

	n = pread(fd, buf, max_size, 0);
	if (n < 0) {
		famfs_log(FAMFS_LOG_ERR,
		       "%s: failed to read max_size=%ld from fd(%d) errno %d\n",
			 __func__, max_size, fd, errno);
		free(buf);
		*size_out = 0;
		return NULL;
	}
	*size_out = n;

	return buf;
}

/*
 * Callback for libcurl to accumulate response data into a buffer.
 */
struct curl_write_ctx {
	char  *buf;
	size_t size;
	size_t alloc;
};

static size_t
curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct curl_write_ctx *ctx = userdata;
	size_t bytes = size * nmemb;
	size_t needed = ctx->size + bytes + 1;

	if (needed > ctx->alloc) {
		size_t newalloc = ctx->alloc ? ctx->alloc * 2 : 4096;

		while (newalloc < needed)
			newalloc *= 2;
		char *newbuf = realloc(ctx->buf, newalloc);

		if (!newbuf)
			return 0; /* Signal error to curl */
		ctx->buf = newbuf;
		ctx->alloc = newalloc;
	}

	memcpy(ctx->buf + ctx->size, ptr, bytes);
	ctx->size += bytes;
	ctx->buf[ctx->size] = '\0';

	return bytes;
}

/**
 * famfs_http_get_uds() - Perform an HTTP GET over a Unix domain socket
 * @socket_path: Path to the Unix domain socket
 * @url_path:    The URL path to request (e.g., "/api/status")
 * @response:    Output pointer to receive the response body (caller must free)
 * @response_len: Output pointer to receive the response length (may be NULL)
 * @http_code:   Output pointer to receive the HTTP status code (may be NULL)
 *
 * Performs an HTTP GET request to a REST server listening on a Unix domain
 * socket. The URL is constructed as "http://localhost<url_path>" but the
 * actual connection goes through the Unix socket.
 *
 * Returns 0 on success, -errno on failure.
 * On success, *response contains a malloc'd null-terminated string that the
 * caller must free.
 */
int
famfs_http_get_uds(
	const char *socket_path,
	const char *url_path,
	char **response,
	size_t *response_len,
	long *http_code)
{
	CURL *curl;
	CURLcode res;
	struct curl_write_ctx ctx = {0};
	char url[1024];
	long code = 0;
	int rc = 0;

	if (!socket_path || !url_path || !response) {
		famfs_log(FAMFS_LOG_ERR, "%s: invalid argument\n", __func__);
		return -EINVAL;
	}

	*response = NULL;
	if (response_len)
		*response_len = 0;
	if (http_code)
		*http_code = 0;

	/* Build URL - hostname is ignored when using Unix socket */
	snprintf(url, sizeof(url), "http://localhost%s", url_path);

	curl = curl_easy_init();
	if (!curl) {
		famfs_log(FAMFS_LOG_ERR, "%s: curl_easy_init failed\n", __func__);
		return -ENOMEM;
	}

	curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socket_path);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		famfs_log(FAMFS_LOG_ERR, "%s: curl error: %s\n",
			 __func__, curl_easy_strerror(res));
		free(ctx.buf);
		curl_easy_cleanup(curl);
		switch (res) {
		case CURLE_COULDNT_CONNECT:
			return -ECONNREFUSED;
		case CURLE_OPERATION_TIMEDOUT:
			return -ETIMEDOUT;
		default:
			return -EIO;
		}
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(curl);

	if (http_code)
		*http_code = code;

	if (code >= 400) {
		famfs_log(FAMFS_LOG_ERR, "%s: HTTP error %ld for %s\n",
			 __func__, code, url_path);
		free(ctx.buf);
		return -EIO;
	}

	*response = ctx.buf;
	if (response_len)
		*response_len = ctx.size;

	return rc;
}
