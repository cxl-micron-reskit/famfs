
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

#include "../tagfs/tagfs.h"
#include "../tagfs/tagfs_ioctl.h"
#include "../tagfs/tagfs_meta.h"

#include "tagfs_lib.h"
#include "bitmap.h"

void
make_bit_string(u8 byte, char *str)
{
        str[0] = (byte & 0x80) ? '1':'0';
        str[1] = (byte & 0x40) ? '1':'0';
        str[2] = (byte & 0x20) ? '1':'0';
        str[3] = (byte & 0x10) ? '1':'0';
        str[4] = (byte & 0x08) ? '1':'0';
        str[5] = (byte & 0x04) ? '1':'0';
        str[6] = (byte & 0x02) ? '1':'0';
        str[7] = (byte & 0x01) ? '1':'0';
        str[8] = 0;
}

void
mu_print_bitmap(u8 *bitmap, int num_bits)
{
        int i, val;

        mu_bitmap_foreach(bitmap, num_bits, i, val) {
                if (!(i%64))
                        printf("\n%4d: ", i);

                printf("%d", val);
        }
        printf("\n");
}


void
tagfs_uuidgen(uuid_le *uuid)
{
	uuid_t local_uuid;

	uuid_generate(local_uuid);
	memcpy(uuid, &local_uuid, sizeof(local_uuid));
}

void
tagfs_print_uuid(const uuid_le *uuid)
{
	uuid_t local_uuid;
	char uuid_str[37];

	memcpy(&local_uuid, uuid, sizeof(local_uuid));
	uuid_unparse(local_uuid, uuid_str);

	printf("%s\n", uuid_str);

}

int
tagfs_get_device_size(const char       *fname,
		      size_t           *size,
		      enum extent_type *type)
{
	char spath[PATH_MAX];
	char npath[PATH_MAX];
	char *rpath, *basename;
	FILE *sfile;
	u_int64_t size_i;
	struct stat st;
	int rc;
	int is_char = 0;

	rc = stat(fname, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to stat file %s (%s)\n",
			__func__, fname, strerror(errno));
		return -errno;
	}

	switch (st.st_mode & S_IFMT) {
	case S_IFBLK:
		printf("%s is a block device\n", fname);
		if (type)
			*type = FSDAX_EXTENT;
		break;
	case S_IFCHR:
		printf("%s character device\n", fname);
		is_char = 1;
		if (type)
			*type = DAX_EXTENT;
		break;
	default:
		fprintf(stderr, "invalid dax device %s\n", fname);
		return -EINVAL;
	}

	basename = strrchr(fname, '/');
	if (is_char) {
		/* character device */
		snprintf(spath, PATH_MAX, "/sys/dev/char/%d:%d/subsystem",
			 major(st.st_rdev), minor(st.st_rdev));

		//basename = get_basename()
		snprintf(spath, PATH_MAX, "/sys/dev/char/%d:%d/size",
			 major(st.st_rdev), minor(st.st_rdev));
	} else {
		/* It's a block device */
		snprintf(spath, PATH_MAX, "/sys/class/block/%s/size", basename);
	}
	printf("checking for size in %s\n", spath);

	sfile = fopen(spath, "r");
	if (!sfile) {
		fprintf(stderr, "%s: fopen on %s failed (%s)\n",
			__func__, spath, strerror(errno));
		return -EINVAL;
	}

	rc = fscanf(sfile, "%lu", &size_i);
	if (rc < 0) {
		fprintf(stderr, "%s: fscanf on %s failed (%s)\n",
			__func__, spath, strerror(errno));
		fclose(sfile);
		return -EINVAL;
	}

	fclose(sfile);

	if (!is_char)
		size_i *= 512; /* Is this always correct?! */

	printf("%s: size=%ld\n", __func__, size_i);
	*size = (size_t)size_i;
	return 0;

}

/**
 * tagfs_append_log()
 *
 * @logp - pointer to struct tagfs_log in memory media
 * @e    - pointer to log entry in memory
 *
 * NOTE: this function is not re-entrant. Must hold a lock or mutex when calling this
 * function if there is any chance of re-entrancy.
 */
int
tagfs_append_log(struct tagfs_log *logp,
		 struct tagfs_log_entry *e)
{
	/* XXX This function is not re-entrant */
	if (!logp || !e)
		return EINVAL;;

	if (logp->tagfs_log_magic != TAGFS_LOG_MAGIC) {
		fprintf(stderr, "Log has invalid magic number\n");
		return EINVAL;
	}

	if (logp->tagfs_log_next_index >= logp->tagfs_log_last_index) {
		fprintf(stderr, "log is full \n");
		return E2BIG;
	}

	e->tagfs_log_entry_seqnum = logp->tagfs_log_next_seqnum++;
	memcpy(&logp->entries[logp->tagfs_log_next_index++], e, sizeof(*e));
	
	return 0;
}


void
print_fsinfo(const struct tagfs_superblock *sb,
	     const struct tagfs_log        *logp,
	     int                            verbose)
{
	size_t  total_log_size;
	int i;

	if (verbose) {
		printf("sizeof superblock: %ld\n", sizeof(struct tagfs_superblock));
		printf("Superblock UUID:   ");
		tagfs_print_uuid(&sb->ts_uuid);

		printf("num_daxdevs:       %d\n", sb->ts_num_daxdevs);
		for (i=0; i<sb->ts_num_daxdevs; i++) {
			if (i==0)
				printf("primary: ");
			else
				printf("       %d: ");

			printf("%s   %lld\n", sb->ts_devlist[i].dd_daxdev, sb->ts_devlist[i].dd_size);
		}

		printf("log_offset:        %ld\n", sb->ts_log_offset);
		printf("log_len:           %ld\n", sb->ts_log_len);

		printf("sizeof(log header) %ld\n", sizeof(struct tagfs_log));
		printf("sizeof(log_entry)  %ld\n", sizeof(struct tagfs_log_entry));

		printf("last_log_index:    %ld\n", logp->tagfs_log_last_index);
		total_log_size = sizeof(struct tagfs_log)
			+ (sizeof(struct tagfs_log_entry) * logp->tagfs_log_last_index);
		printf("full log size:     %ld\n", total_log_size);
		printf("TAGFS_LOG_LEN:     %ld\n", TAGFS_LOG_LEN);
		printf("Remainder:         %ld\n", TAGFS_LOG_LEN - total_log_size);
		printf("\nfc: %ld\n", sizeof(struct tagfs_file_creation));
		printf("fa:   %ld\n", sizeof(struct tagfs_file_access));
	}

}

/**
 * tagfs_mmap_superblock_and_log_raw()
 *
 * This function SHOULD ONLY BE CALLED BY FSCK AND MKMETA
 *
 * The superblock and log are mapped directly from a device. Other apps should map
 * them from their meta files!
 *
 * @devname   - dax device name
 * @sbp
 * @logp
 * @read_only - map sb and log read-only
 */
int
tagfs_mmap_superblock_and_log_raw(const char *devname,
				  struct tagfs_superblock **sbp,
				  struct tagfs_log **logp,
				  int read_only)
{
	int fd = 0;
	void *sb_buf;
	void *log_buf;
	int rc = 0;
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	int mapmode  = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;

	fd = open(devname, openmode, 0);
	if (fd < 0) {
		fprintf(stderr, "open/create failed; rc %d errno %d\n", rc, errno);
		rc = -1;
		goto err_out;
	}

	/* Map superblock and log in one call */
	sb_buf = mmap(0, TAGFS_SUPERBLOCK_SIZE + TAGFS_LOG_LEN, mapmode, MAP_SHARED, fd, 0);
	if (sb_buf == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap superblock and log from %s\n", devname);
		rc = -1;
		goto err_out;
	}
	*sbp = (struct tagfs_superblock *)sb_buf;

	*logp = (struct tagfs_log *)((u64)sb_buf + TAGFS_SUPERBLOCK_SIZE);
	close(fd);
	return 0;

err_out:
	if (sb_buf)
		munmap(sb_buf, TAGFS_SUPERBLOCK_SIZE);

	if (log_buf)
		munmap(log_buf, TAGFS_LOG_LEN);

	if (fd)
		close(fd);
	return rc;
}

int
tagfs_check_super(const struct tagfs_superblock *sb)
{
	if (!sb)
		return -1;
	if (sb->ts_magic != TAGFS_SUPER_MAGIC)
		return -1;
	/* TODO: check crc, etc. */
	return 0;
}

int
tagfs_fsck(const char *devname,
	   int verbose)
{
	struct tagfs_superblock *sb;
	struct tagfs_log *logp;
	size_t size;
	int rc;

	rc = tagfs_get_device_size(devname, &size, NULL);
	if (rc < 0)
		return -1;

	printf("device: %s\n", devname);
	printf("size:   %ld\n", size);
	rc = tagfs_mmap_superblock_and_log_raw(devname, &sb, &logp, 1 /* read-only */);

	if (tagfs_check_super(sb)) {
		fprintf(stderr, "%s: no tagfs superblock on device %s\n", __func__, devname);
		return -1;
	}

	print_fsinfo(sb, logp, verbose);
	return 0;
}

#define XLEN 256

/**
 * tagfs_get_mpt_by_dev()
 *
 * @mtdev = the primary dax device for a tagfs file system.
 *
 * This function determines the mount point by parsing /proc/mounts to find the mount point
 * from a dax device name.
 */
static char *
tagfs_get_mpt_by_dev(const char *mtdev)
{
	FILE * fp;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	char *devstr = 0;
	int rc;
	char *answer = NULL;

	fp = fopen("/proc/mounts", "r");
	if (fp == NULL)
		exit(EXIT_FAILURE);

	while ((read = getline(&line, &len, fp)) != -1) {
		char dev[XLEN];
		char mpt[XLEN];
		char fstype[XLEN];
		char args[XLEN];
		int  x0, x1;
		char *xmpt = NULL;

		if (strstr(line, "tagfs")) {
			rc = sscanf(line, "%s %s %s %s %d %d",
				    dev, mpt, fstype, args, &x0, &x1);
			xmpt = realpath(mpt, NULL);
			if (!xmpt) {
				fprintf(stderr, "realpath(%s) errno %d\n", mpt, errno);
				continue;
			}
			if (strcmp(dev, mtdev) == 0) {
				answer = strdup(xmpt);
				free(line);
				return answer;
			}
		}

	}

	fclose(fp);
	if (line)
		free(line);
	return NULL;
}

/**
 * tagfs_mkmeta()
 *
 * @devname - primary device for a tagfs file system
 */
int
tagfs_mkmeta(const char *devname)
{
	struct stat st = {0};
	int rc, sbfd, logfd;;
	char *mpt = NULL;
	char dirpath[PATH_MAX];
	char sb_file[PATH_MAX];
	char log_file[PATH_MAX];
	struct tagfs_ioc_map sb_map = {0};
	struct tagfs_ioc_map log_map = {0};
	struct tagfs_superblock *sb;
	struct tagfs_log *logp;

	dirpath[0] = 0;

	/* Get mount point path */
	mpt = tagfs_get_mpt_by_dev(devname);
	if (!mpt) {
		fprintf(stderr, "%s: unable to resolve mount pt from dev %s\n", __func__, devname);
		return -1;
	}
	printf("mpt: %s\n", mpt);
	strncat(dirpath, mpt,     PATH_MAX - 1);
	strncat(dirpath, "/",     PATH_MAX - 1);
	strncat(dirpath, ".meta", PATH_MAX - 1);

	/* Create the meta directory */
	if (stat(dirpath, &st) == -1) {
		rc = mkdir(dirpath, 0700);
		if (rc) {
			fprintf(stderr, "%s: error creating directory %s\n", __func__, dirpath);
		}
	}

	/* Create the superblock file */
	strncpy(sb_file, dirpath, PATH_MAX - 1);
	strncpy(log_file, dirpath, PATH_MAX - 1);

	strncat(sb_file, "/.superblock", PATH_MAX - 1);
	strncat(log_file, "/.log", PATH_MAX - 1);

	rc = tagfs_mmap_superblock_and_log_raw(devname, &sb, &logp, 1);
	if (rc) {
		fprintf(stderr, "%s: superblock/log accessfailed\n", __func__);
		return -1;
	}

	if (tagfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock on device %s\n", __func__, devname);
		return -1;
	}

	/* Create and allocate Superblock file */
	sbfd = open(sb_file, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	if (sbfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, sb_file);
		return -1;
	}
	sb_map.extent_type    = FSDAX_EXTENT;
	sb_map.file_size      = TAGFS_SUPERBLOCK_SIZE;
	sb_map.ext_list_count = 1;

	sb_map.ext_list = calloc(1, sizeof(struct tagfs_extent));
	sb_map.ext_list[0].offset = 0;
	sb_map.ext_list[0].len = TAGFS_SUPERBLOCK_SIZE;

	rc = ioctl(sbfd, TAGFSIOC_MAP_CREATE, &sb_map);
	if (sbfd < 0) {
		fprintf(stderr, "MAP_CREATE failed for %s; rc %d errno %d\n",
			sb_file, rc, errno);
		unlink(sb_file);
		return -1;
	}

	/* Create and allocate log file */
	logfd = open(log_file, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	if (logfd < 0) {
		fprintf(stderr, "%s: failed to create file %s\n", __func__, log_file);
		return -1;
	}

	log_map.extent_type     = FSDAX_EXTENT;
	log_map.file_size       = sb->ts_log_len;
	log_map.ext_list_count  = 1;
	log_map.ext_list = calloc(1, sizeof(struct tagfs_extent));
	log_map.ext_list[0].offset = sb->ts_log_offset;
	log_map.ext_list[0].len = sb->ts_log_len;

	rc = ioctl(logfd, TAGFSIOC_MAP_CREATE, &log_map);
	if (rc) {
		fprintf(stderr, "MAP_CREATE failed for %s; rc %d errno %d\n",
			sb_file, rc, errno);
		unlink(log_file);
		return -1;
	}

	close(sbfd);
	close(logfd);
	return 0;
}

void *
mmap_whole_file(const char *fname,
		int read_only)
{
	struct stat st;
	void *addr;
	int rc, fd;
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	int mapmode  = (read_only) ? PROT_READ : PROT_READ | PROT_WRITE;

	rc = stat(fname, &st);
	if (rc < 0) {
		fprintf(stderr, "%s: failed to stat file %s (%s)\n",
			__func__, fname, strerror(errno));
		return NULL;
	}
	switch (st.st_mode & S_IFMT) {
	case S_IFREG:
		printf("regular file\n");
		break;
	default:
		printf("unknown?\n");
                break;
	}

	fd = open(fname, openmode, 0);
	if (fd < 0) {
		fprintf(stderr, "open %s failed; rc %d errno %d\n", fname, rc, errno);
		rc = -1;
		return NULL;
	}

	addr = mmap(0, st.st_size, mapmode, MAP_SHARED, fd, 0);
	if (addr== MAP_FAILED) {
		fprintf(stderr, "Failed to mmap file %s\n", fname);
		rc = -1;
		close(fd);
		return NULL;
	}
	return addr;
}

struct tagfs_superblock *
mmap_superblock_file_read_only(const char *mpt)
{
	char sb_path[PATH_MAX];

	memset(sb_path, 0, PATH_MAX);

	strncat(sb_path, mpt,     PATH_MAX - 1);
	strncat(sb_path, "/", PATH_MAX - 1);
	strncat(sb_path, SB_FILE_RELPATH, PATH_MAX - 1);

	return mmap_whole_file(sb_path, 1 /* superblock file always read-only */);
}

static struct tagfs_log *
__mmap_log_file(const char *mpt,
		int read_only)
{
	char log_path[PATH_MAX];

	memset(log_path, 0, PATH_MAX);

	strncat(log_path, mpt,     PATH_MAX - 1);
	strncat(log_path, "/", PATH_MAX - 1);
	strncat(log_path, LOG_FILE_RELPATH, PATH_MAX - 1);

	return mmap_whole_file(log_path, read_only);
}

static struct tagfs_log *
mmap_log_file_read_only(const char *mpt)
{
	return __mmap_log_file(mpt, 1);
}

static struct tagfs_log *
mmap_log_file_writable(const char *mpt)
{
	return __mmap_log_file(mpt, 0);
}

int
tagfs_logplay(const char *daxdev)
{
	char *mpt;
	struct tagfs_superblock *sb;
	struct tagfs_log *logp;
	int nlog = 0;
	int i;

	mpt = tagfs_get_mpt_by_dev(daxdev);
	if (!mpt) {
		fprintf(stderr, "%s: unable to resolve mount point path from dev %s\n",
			__func__, mpt);
		return -1;
	}

	sb = mmap_superblock_file_read_only(mpt);
	if (sb == MAP_FAILED) {
		fprintf(stderr, "%s: failed to mmap superblock file\n", __func__);
		return -1;
	}
	logp = mmap_log_file_read_only(mpt);
	if (sb == MAP_FAILED) {
		fprintf(stderr, "%s: failed to mmap log file\n", __func__);
		return -1;
	}

	if (tagfs_check_super(sb)) {
		fprintf(stderr, "%s: no valid superblock on device %s\n", __func__, daxdev);
		return -1;
	}

	if (logp->tagfs_log_next_index == 0) {
		fprintf(stderr, "%s: log is empty (dev=%s, mpt=%s)\n", __func__, daxdev, mpt);
		return -1;
	}

	printf("%s: log contains %ld entries\n", __func__, logp->tagfs_log_next_index);
	for (i=0; i<logp->tagfs_log_next_index; i++) {
		struct tagfs_log_entry le = logp->entries[i];

		nlog++;
		switch (le.tagfs_log_entry_type) {
		case TAGFS_LOG_FILE: {
			struct tagfs_file_creation *fc = &le.tagfs_fc;

			printf("%: file=%s size=%ld", __func__,
			       fc->tagfs_relpath, fc->tagfs_fc_size);
			break;
		}
		case TAGFS_LOG_ACCESS:
		default:
			printf("%s: invalid log entry\n", __func__);
			break;
		}
	}
	printf("%s: processed %d log entries\n", __func__, nlog);
	return 0;
}

int
__open_relpath(const char *path,
	       const char *relpath,
	       int         read_only,
	       size_t     *size)
{
	int openmode = (read_only) ? O_RDONLY : O_RDWR;
	char *rpath = realpath(path, NULL);
	struct stat st;
	int rc, fd;

	if (!rpath)
		return 0;

	while (1) {
		char log_path[PATH_MAX] = {0};

		rc = stat(rpath, &st);
		if (rc < 0)
			goto next;
		if ((st.st_mode & S_IFMT) == S_IFDIR) {
			/* It's a dir; does it have <relpath> under it? */
			snprintf(log_path, PATH_MAX - 1, "%s/%s", rpath, relpath);
			rc = stat(log_path, &st);
			if ((rc == 0) && ((st.st_mode & S_IFMT) == S_IFREG)) {
				/* yes */
				*size = st.st_size;
				fd = open(log_path, openmode, 0);
				return fd;
			}
			/* no */
		}

	next:
		/* pop up one level */
		rpath = dirname(rpath);
		if (strcmp(rpath, ".") == 0)
			break;
	}
	return -1;
}


/**
 * open_log_file(path)
 *
 * @path - a path within a tagfs file system
 *
 * This will traverse upward from path, looking for a directory containing a .meta/.log
 * If found, it opens the log.
 */
static int
__open_log_file(const char *path,
	      int         read_only,
	      size_t     *size)
{
	return __open_relpath(path, LOG_FILE_RELPATH, read_only, size);
}

int
open_log_file_read_only(const char *path,
			size_t     *sizep)
{
	return __open_log_file(path, 1, sizep);
}

int
open_log_file_writable(const char *path,
		       size_t     *sizep)
{
	return __open_log_file(path, 0, sizep);
}

static int
__open_superblock_file(const char *path,
		       int         read_only,
		       size_t     *sizep)
{
	return __open_relpath(path, SB_FILE_RELPATH, read_only, sizep);
}

int
open_superblock_file_read_only(const char *path,
				size_t     *sizep)
{
	return __open_superblock_file(path, 1, sizep);
}

int
open_superblock_file_writable(const char *path,
			      size_t     *sizep)
{
	return __open_superblock_file(path, 0, sizep);
}

/**
 * tagfs_build_bitmap()
 *
 * XXX: this is only aware of the first daxdev in the superblock's list
 */
u8 *
tagfs_build_bitmap(const struct tagfs_superblock *sb,
		   struct tagfs_log              *logp,
		   u64                            size_in,
		   u64                           *size_out)
{
	int npages = (size_in - TAGFS_SUPERBLOCK_SIZE - TAGFS_LOG_LEN) / TAGFS_ALLOC_UNIT;
	int bitmap_size = mu_bitmap_size(npages);
	u8 *bitmap = calloc(1, bitmap_size);
	int i, j;

	if (!bitmap)
		return NULL;

	/* Mark superblock and log as allocated */
	/* XXX: currently there are no log entries for superblock and log */
	mu_bitmap_set(bitmap, 0);

	for (i=1; i<((TAGFS_LOG_OFFSET + TAGFS_LOG_LEN) / TAGFS_ALLOC_UNIT); i++)
		mu_bitmap_set(bitmap, i);

	/* This loop is over all log entries */
	for (i=0; i<logp->tagfs_log_next_index; i++) {
		struct tagfs_log_entry *le = &logp->entries[i];

		/* TODO: validate log sequence number */

		switch (le->tagfs_log_entry_type) {
		case TAGFS_LOG_FILE: {
			struct tagfs_file_creation *fc = &le->tagfs_fc;
			struct tagfs_log_extent *ext = fc->tagfs_log;

			printf("%: file=%s size=%ld", __func__,
			       fc->tagfs_relpath, fc->tagfs_fc_size);

			/* For each extent in this log entry, mark the bitmap as allocated */
			for (j=0; j<fc->tagfs_nextents; j++) {
				s64 page_num;
				s64 np;
				s64 k;

				assert(!(ext[j].se.tagfs_extent_offset % TAGFS_ALLOC_UNIT));
				page_num = ext[j].se.tagfs_extent_offset / TAGFS_ALLOC_UNIT;
				np = (ext[j].se.tagfs_extent_len + TAGFS_ALLOC_UNIT - 1)
					/ TAGFS_ALLOC_UNIT;

				for (k=page_num; k<(page_num + np); k++) {
					mu_bitmap_set(bitmap, k);
				}
			}
			break;
		}
		case TAGFS_LOG_ACCESS:
		default:
			printf("%s: invalid log entry\n", __func__);
			break;
		}
	}
	*size_out = bitmap_size;
	return bitmap;
}

/**
 * bitmap_alloc_contigous()
 *
 * @bitmap
 * @nbits - number of bits in the bitmap
 * @size - size to allocate in bytes (must convert to bits)
 *
 * Return value: the offset in bytes
 */
u64
bitmap_alloc_contiguous(u8 *bitmap,
			u64 nbits,
			u64 size)
{
	int i, j;

	int alloc_bits = (size + TAGFS_ALLOC_UNIT - 1) /  TAGFS_ALLOC_UNIT;
	for (i=0; i<nbits; i++) {
		/* Skip bits that are set... */
		if (mu_bitmap_test(bitmap, i))
			continue;

		for (j=i; j<(i+alloc_bits); j++) {
			if (mse_bitmap_test32(bitmap, j))
				goto next;
		}
		/* If we get here, we didn't hit the "continue" which means that bits
		 * i-(i+alloc_bits) are available
		 */
		for (j=i; j<(i+alloc_bits); j++)
			mse_bitmap_set32(bitmap, j);

		return i * TAGFS_ALLOC_UNIT;
	next:
	}
	fprintf(stderr, "%s: alloc failed\n", __func__);
	return 0;
}

/**
 * tagfs_alloc_bypath()
 *
 * @path    - a path within the tagfs file system
 * @size    - size in bytes
 *
 * XXX currently only contiuous allocations are supported
 */
s64
tagfs_alloc_bypath(const char *path,
		   u64 size)
{
	char *mpt;
	struct tagfs_superblock *sb;
	struct tagfs_log *logp;
	void *addr;
	int nlog = 0;
	u8 *bitmap;
	u64 nbits;
	u64 offset;
	int sfd, lfd;
	size_t sb_size, log_size;
	u64 daxdevsize;

	if (size <= 0)
		return -1;
#if 1
	/* XXX should be read only, but that doesn't work */
	sfd = open_superblock_file_writable(path, &sb_size);
	if (sfd < 0)
		return sfd;

	/* XXX should be read only, but that doesn't work */
	addr = mmap(0, sb_size, O_RDWR, MAP_SHARED, sfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap superblock file\n", __func__);
		close(sfd);
		return -1;
	}
	sb = (struct tagfs_superblock *)addr;

	if (tagfs_check_super(sb)) {
		fprintf(stderr, "%s: invalid superblock\n", __func__);
		return -1;
	}
	daxdevsize = sb->ts_devlist[0].dd_size;
	munmap(sb, TAGFS_SUPERBLOCK_SIZE);
	close(sfd);
#endif

	/* Log file */
	lfd = open_log_file_writable(path, &log_size);
	if (lfd < 0)
		return lfd;

	addr = mmap(0, log_size, O_RDWR, MAP_SHARED, lfd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Failed to mmap log file", __func__);
		close(lfd);
		return -1;
	}
	close(lfd);
	logp = (struct tagfs_log *)addr;

	bitmap = tagfs_build_bitmap(sb, logp, daxdevsize, &nbits);
	printf("\nbitmap before:\n");
	mu_print_bitmap(bitmap, nbits);
	offset = bitmap_alloc_contiguous(bitmap, nbits, size);
	printf("\nbitmap after:\n");
	mu_print_bitmap(bitmap, nbits);
	printf("\nAllocated offset: %lld\n", offset);
	return offset;
}

int
__file_not_tagfs(int fd)
{
	int rc;

	rc = ioctl(fd, TAGFSIOC_NOP, 0);
	if (rc)
		return 1;

	return 0;
}

/**
 * tagfs_file_alloc()
 *
 * Alllocate space for a file, making it ready to use
 */
static int
tagfs_file_alloc(int         fd,
		 const char *path,
		 u64         size)
{
	struct tagfs_ioc_map filemap = {0};
	struct tagfs_extent ext = {0};
	s64 offset;
	int rc;

	assert(fd > 0);

	/* Allocation is always contiguous initially */
	offset = tagfs_alloc_bypath(path, size);
	if (offset < 0)
		return -1;

	filemap.file_size = size;
	filemap.extent_type = FSDAX_EXTENT;
	filemap.ext_list_count = 1;

	/* Round length up to 2 MiB boundary */
	ext.len = ((size + TAGFS_ALLOC_UNIT - 1) / TAGFS_ALLOC_UNIT) * TAGFS_ALLOC_UNIT;
	ext.offset = offset;
	filemap.ext_list = &ext; /* XXX only one extent so this is a list */

	rc = ioctl(fd, TAGFSIOC_MAP_CREATE, &filemap);
	if (rc) {
		printf("ioctl returned rc %d errno %d\n", rc, errno);
		perror("ioctl");
		return rc;
	}
	return 0;
}

/**
 * tagfs_file_create()
 *
 * Create a file but don't allocate dax space yet
 */
static int
tagfs_file_create(const char *path,
		  mode_t mode,
		  uid_t uid,
		  gid_t gid,
		  size_t size)
{
	int rc = 0;
	int fd = open(path, O_RDWR | O_CREAT, mode);

	if (fd < 0)
		return rc;

	if (__file_not_tagfs(fd)) {
		close(fd);
		unlink(path);
		return -EBADF;
	}
	return fd;
}


/**
 * libtagfs:
 *
 * tagfs_cp(srcfile, destfile)
 */

int
tagfs_cp(char *srcfile,
	 char *destfile)
{
	struct stat srcstat;
	struct stat deststat;
	int rc, srcfd, destfd;
	char *destp;

	size_t chunksize, remainder, offset;
	ssize_t bytes;


	/**
	 * Check the destination file first, since that is constrained in several ways:
	 * * Dest must be in a tagfs file system
	 * * Dest must not exist already
	 */
	rc = stat(destfile, &deststat);
	if (!rc) {
		fprintf(stderr, "%s: error: dest destfile (%s) exists\n", __func__, destfile);
		return rc;
	}
	rc = stat(srcfile, &srcstat);
	if (rc) {
		fprintf(stderr, "%s: unable to stat srcfile (%s)\n", __func__, srcfile);
		return rc;
	}

	destfd = tagfs_file_create(destfile, srcstat.st_mode, srcstat.st_uid, srcstat.st_gid,
				   srcstat.st_size);
	if (destfd < 0) {
		if (destfd == -EBADF)
			fprintf(stderr, "Destination file %s is not in a tagfs file system\n",
				destfile);
		else
			fprintf(stderr, "%s: unable to create destfile (%s)\n", __func__, destfile);

		unlink(destfile);
		return destfd;
	}

	/*
	 * Now deal with source file
	 */
	srcfd = open(srcfile, O_RDONLY, 0);
	if (srcfd < 0) {
		fprintf(stderr, "%s: unable to open srcfile (%s)\n", __func__, srcfile);
		unlink(destfile);
		return rc;
	}

	rc = tagfs_file_alloc(destfd, destfile, srcstat.st_size);
	if (rc) {
		fprintf(stderr, "%s: failed to allocate size %ld for file %s\n",
			__func__, srcstat.st_size, destfile);
		unlink(destfile);
		return -1;
	}

	destp = mmap(0, srcstat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, destfd, 0);
	if (destp == MAP_FAILED) {
		fprintf(stderr, "%s: dest mmap failed\n", __func__);
		unlink(destfile);
		return -1; /* XXX */
	}

	/* Copy the data */
	chunksize = 0x100000; /* 1 MiB copy chunks */
	offset = 0;
	remainder = srcstat.st_size;
	for ( ; remainder > 0; ) {
		size_t cur_chunksize = MIN(chunksize, remainder);

		/* read into mmapped destination */
		bytes = read(srcfd, &destp[offset], cur_chunksize);
		if (bytes < 0) {
			fprintf(stderr, "%s: copy fail: "
				"size %lld ofs %lld cur_chunksize %lld remainder %lld\n",
				__func__, offset, cur_chunksize, remainder);
			printf("rc=%lld errno=%d\n", bytes, errno);
			return -1;
		}
		if (bytes < cur_chunksize) {
			fprintf(stderr, "%s: short read: "
				"size %lld ofs %lld cur_chunksize %lld remainder %lld\n",
				__func__, offset, cur_chunksize, remainder);
		}

		/* Update offset and remainder */
		offset += bytes;
		remainder -= bytes;
	}

	close(srcfd);
	close(destfd);
	return 0;
}
