// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2024 Micron Technology, Inc.  All rights reserved.
 */
#include <stdio.h>
#include <stdarg.h>
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
#include "famfs_meta.h"
#include "famfs_fmap.h"
#include "fuse_kernel.h"

void pr_verbose(int verbose, const char *format, ...) {
	if (!verbose) {
		return;
	}

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

void
free_mem_fmap(struct fmap_mem_header *fm)
{
	int i;

	if (!fm)
		return;

	assert(fm->flh.struct_tag == LOG_HEADER_TAG);

	switch (fm->flh.fmap_ext_type) {
	case FAMFS_EXT_SIMPLE:
		if (fm->se)
			free(fm->se);
		break;
	case FAMFS_EXT_INTERLEAVE:
		if (!fm->ie)
			break;
		for (i = 0; i < fm->flh.niext; i++) {
			assert(fm->ie[i].iext.struct_tag == LOG_IEXT_TAG);
			if (fm->ie[i].se)
				free(fm->ie[i].se);
		}
		free(fm);
		break;
	}
}

static struct fmap_mem_header *
alloc_mem_fmap(void)
{
	struct fmap_mem_header *fm = calloc(1, sizeof(*fm));

	if (!fm)
		return NULL;

	fm->flh.struct_tag = LOG_HEADER_TAG;

	return fm;
}

static struct fmap_simple_ext *
alloc_simple_extlist(int next)
{
	struct fmap_simple_ext *se;
	int i;

	se = calloc(next, sizeof(*se));
	if (!se)
		return NULL;

	for (i = 0; i < next; i++)
		se[i].struct_tag = LOG_SIMPLE_EXT_TAG;

	return se;
}

/**
 * alloc_interleaved_fmap(): This allocates an interleaved fmap
 *
 * @ninterleave - number of interleaved extents
 * @nstrips_per_interleave - number of strips per interleaved extent
 *
 * struct_tags are initialized, but extents and strips are not initialized
 * (other than their struct tags)
 *
 * The following are left uninitialized:
 * * ie_chunk_size
 * * ie_nbytes
 * * simple extents for strips (except the struct_tag, which is initialized)
 */
struct fmap_mem_header *
alloc_interleaved_fmap(
	int ninterleave,
	int nstrips_per_interleave,
	int verbose)
{
	struct fmap_mem_header *fm = alloc_mem_fmap();
	int i;

	if (!fm)
		return NULL;
	if (ninterleave > FAMFS_MAX_SIMPLE_EXT)
		goto out_free;
	if (nstrips_per_interleave == 0)
		goto out_free;
	if (nstrips_per_interleave > FAMFS_MAX_SIMPLE_EXT)
		goto out_free;

	fm->flh.fmap_ext_type = FUSE_FAMFS_EXT_INTERLEAVE;
	fm->flh.niext = ninterleave;

	pr_verbose(verbose, "%s: ninterleave=%d sizeof(ie)=%ld\n",
		   __func__, ninterleave, sizeof(*(fm->ie)));
	fm->ie = calloc(ninterleave, sizeof(*(fm->ie)));

	if (!fm->ie)
		goto out_free;

	for (i = 0; i < ninterleave; i++) {
		pr_verbose(verbose, "%s(%d): %p set LOG_IEXT_TAG\n",
			   __func__, i, &(fm->ie[i]));
		fm->ie[i].iext.struct_tag = LOG_IEXT_TAG;
		fm->ie[i].iext.ie_nstrips = nstrips_per_interleave;
		fm->ie[i].se = alloc_simple_extlist(nstrips_per_interleave);
	}

	pr_verbose(verbose, "%s: success(%d, %d)\n", __func__,
		   ninterleave, nstrips_per_interleave);
	pr_verbose(verbose, "%s: dumping:\n", __func__);
	validate_mem_fmap(fm, 0, 1);
	return fm;

out_free:
	pr_verbose(verbose, "%s: error\n", __func__);
	free(fm);
	return NULL;
}

/**
 * alloc_simple_fmap()
 *
 * This allocates a simple fmap, which is valid except that the extents
 * are not filled in.
 */
struct fmap_mem_header *
alloc_simple_fmap(int next)
{
	struct fmap_mem_header *fm = alloc_mem_fmap();

	if (!fm)
		return NULL;
	if (next == 0)
		goto out_free;
	if (next > FAMFS_MAX_SIMPLE_EXT)
		goto out_free;

	fm->flh.fmap_ext_type = FAMFS_EXT_SIMPLE;
	fm->flh.next = next;
	fm->se = alloc_simple_extlist(next);

	if (!fm->se)
		goto out_free;

	return fm;

out_free:
	free(fm);
	return NULL;
		
}

ssize_t
famfs_log_file_meta_to_msg(
	char *msg,
	int msg_size,
	int file_type,
	const struct famfs_log_file_meta *fmeta)
//	const struct famfs_log_fmap *log_fmap)
{
	struct fuse_famfs_fmap_header *flh = (struct fuse_famfs_fmap_header *)msg;
	const struct famfs_log_fmap *log_fmap = &fmeta->fm_fmap;
	int cursor = 0;
	int i, j;

	if (msg_size < sizeof(*flh))
		return -EINVAL;

	flh->fmap_version = FAMFS_FMAP_VERSION;
	flh->file_type = file_type;
	//flh->ext_type = log_fmap->fmap_ext_type;
	flh->ext_type = fmeta->fm_fmap.fmap_ext_type;
	flh->file_size = fmeta->fm_size;
	switch (flh->ext_type) {
	case FAMFS_EXT_SIMPLE:
		flh->nextents = fmeta->fm_fmap.fmap_nextents;
		break;
	case FAMFS_EXT_INTERLEAVE:
		flh->nextents = fmeta->fm_fmap.fmap_niext;
		break;
	default:
		goto err_out;
	}

	cursor += sizeof(*flh);

	printf("%s: size=%ld ext_type=%d nextents=%d\n",
	       __func__, flh->file_size, flh->ext_type, flh->nextents);
	famfs_emit_file_yaml(fmeta, stdout); /* needs famfs_lib.h */
	switch (log_fmap->fmap_ext_type) {
	case FUSE_FAMFS_EXT_SIMPLE: {
		struct fuse_famfs_simple_ext *se = (struct fuse_famfs_simple_ext *)&msg[cursor];
		size_t ext_list_size = log_fmap->fmap_nextents * sizeof(*se);

		cursor += ext_list_size;
		if (cursor > msg_size)
			goto err_out;

		flh->nextents = log_fmap->fmap_nextents;

		memset(se, 0, ext_list_size);
		for (i = 0; i < flh->nextents; i++) {
			memset(&se[i], 0, sizeof(se[i]));
			se[i].se_devindex = log_fmap->se[i].se_devindex;
			se[i].se_offset = log_fmap->se[i].se_offset;
			se[i].se_len = log_fmap->se[i].se_len;
		}

		break;
	}
	case FAMFS_EXT_INTERLEAVE: {
		struct fuse_famfs_iext *ie = (struct fuse_famfs_iext *)&msg[cursor];
		struct fmap_simple_ext *se;

		/* There can be more than one interleaved extent */
		for (i = 0; i < log_fmap->fmap_niext; i++) {
			cursor += sizeof(*ie);
			if (cursor > msg_size)
				goto err_out;

			/* Interleaved extent header into msg */
			memset(ie, 0, sizeof(*ie));

			ie[i].ie_nstrips = log_fmap->ie[i].ie_nstrips;
			ie[i].ie_chunk_size = log_fmap->ie[i].ie_chunk_size;
			// XXX ie[i].ie_nbytes = log_fmap->ie[i].ie_nbytes;
			ie[i].ie_nbytes = fmeta->fm_size;

			printf("%s: ie[%d] nstrips=%d chunk=%d nbytes=%ld\n",
			       __func__, i, ie[i].ie_nstrips, ie[i].ie_chunk_size,
			       ie[i].ie_nbytes);
			se = (struct fmap_simple_ext *)&msg[cursor];

			cursor += ie[i].ie_nstrips * sizeof(*se);
			if (cursor > msg_size)
				goto err_out;

			memset(se, 0, ie[i].ie_nstrips * sizeof(*se));

			printf("%s: interleaved ext %d: strips=%d\n",
			       __func__, i, ie[i].ie_nstrips);
			/* Strip extents into msg */
			for (j = 0; j < ie[i].ie_nstrips; j++) {
				const struct famfs_simple_extent *strips =
					log_fmap->ie[i].ie_strips;

				se[j].se_devindex = strips[j].se_devindex;
				se[j].se_offset   = strips[j].se_offset;
				se[j].se_len      = strips[j].se_len;
			}
		}
		break;
	}
	default:
#if 0
		fuse_log(FUSE_LOG_ERR, "%s: invalid ext type %d\n",
			 __func__, log_fmap->fmap_ext_type);
#endif
		return -EINVAL;
	}

	return cursor;

err_out:
#if 0
	fuse_log(FUSE_LOG_ERR, "%s: buffer overflow\n",
		 __func__);
#endif
	return -EINVAL;
}

#if 0
int
copy_to_cursor(
	void **dest,
	void *src,
	uint count,
	size_t len)
{
	size_t size = count * len;

	assert(size >= 0);
	if (size == 0)
		return 0;

	memcpy(*dest, src, size);
	*dest += size;
}

ssize_t
write_fmap_to_buf(
	const struct fmap_log_header *fmap,
	void *buf,
	size_t buflen)
{
	struct fmap_log_header *fmh = buf;

	memset(buf, 0, buflen);

	fmh->struct_tag = LOG_HEADER_TAG;
	fmh->fmap_log_version = FAMFS_LOG_VERSION;
	fmh->fmap_ext_type = fmap->fmap_ext_type;
}

static ssize_t
get_simple_ext_list(
	const void *buf,
	ssize_t buf_remainder,
	size_t next,
	struct fmap_simple_ext **se_out)
{
	ssize_t se_size = next * sizeof(**se_out);
	struct fmap_simple_ext *se;

	if (next > FAMFS_MAX_SIMPLE_EXT)
		return -EINVAL;

	if (se_size > buf_remainder)
		return -EINVAL;

	se = calloc(next, sizeof(*se));
	if (!se)
		return -ENOMEM;

	memcpy(&se, buf, se_size);
	*se_out = se;

	return se_size;
}

static ssize_t
get_interleaved_ext_list(
	void *buf,
	ssize_t buflen,
	struct fmap_mem_iext **ie_out)
{
	ssize_t ie_size = 0;
	struct fmap_simple_ext *se = NULL;
	struct fmap_mem_iext *local_ie = NULL;
	ssize_t buf_remainder = buflen;
	void *localbuf = buf;
	ssize_t se_size;
	int rc;

	ie_size = local_ie->iext.ie_nstrips * sizeof(*se);

	if (ie_size > buf_remainder)
		return -EINVAL;

	/* Allocate a header */
	local_ie = calloc(local_ie->iext.ie_nstrips, sizeof(*local_ie));
	if (!local_ie)
		return -ENOMEM;

	memcpy(local_ie, buf, ie_size);

	if (local_ie->iext.struct_tag != LOG_IEXT_TAG) {
		rc = -EINVAL;
		goto err_out;
	}

	if (local_ie->iext.reserved != 0) {
		rc = -EINVAL;
		goto err_out;
	}

	/* Move past header */
	localbuf += ie_size;
	buf_remainder -= ie_size;

	if (local_ie->iext.ie_nstrips > FAMFS_MAX_STRIPS) {
		rc = -EINVAL;
		goto err_out;
	}

	se_size = get_simple_ext_list(localbuf, buf_remainder,
				      local_ie->iext.ie_nstrips, &se);
	if (se_size <= 0) {
		if (se_size < 0)
			rc = se_size;
		else
			rc = -EFAULT;
		goto err_out;
	}

	local_ie->se = se;
	*ie_out = local_ie;
	return ie_size + se_size;

err_out:
	if (se)
		free(se);
	if (local_ie)
		free(local_ie);
	return rc;
}

/*
 * Allocate an in-memory fmap struct and read read a message-format
 * fmap into it
 */
ssize_t
read_fmap_from_buf(
	struct fmap_mem_header *fmap_out,
	void *buf,
	size_t buflen)
{
	struct fmap_mem_header *lfmap = NULL;
	ssize_t buf_remainder = buflen;
	void *localbuf = buf;
	int rc;

	lfmap = calloc(1, sizeof(struct fmap_mem_header));
	if (!lfmap) {
		rc = -ENOMEM;
		goto err_out;
	}
	memcpy(&lfmap->flh, buf, sizeof(lfmap->flh));

	if (lfmap->flh.struct_tag != LOG_HEADER_TAG) {
		rc = -EINVAL;
		goto err_out;
	}

	if (lfmap->flh.fmap_log_version != FAMFS_LOG_VERSION) {
		rc =-EINVAL;
		goto err_out;
	}

	if (lfmap->flh.reserved != 0) {
		rc =-EINVAL;
		goto err_out;
	}

	buf_remainder -= sizeof(*lfmap);
	localbuf +=  sizeof(*lfmap);

	switch (lfmap->flh.fmap_ext_type) {
	case FAMFS_EXT_SIMPLE: {
		struct fmap_simple_ext *se = NULL;
		ssize_t se_size = 0;;

		rc = get_simple_ext_list(localbuf, buf_remainder,
					 lfmap->flh.next, &se);
		if (rc)
			goto err_out;

		buf_remainder -= se_size;
		localbuf += se_size;
		break;
	}
	case FAMFS_EXT_INTERLEAVE: {
		/* TODO: for loop over lfmap->ie.next */
		struct fmap_mem_iext *ie = NULL;
		ssize_t ie_size = 0;

		ie_size = get_interleaved_ext_list(localbuf, buf_remainder,
					      &ie);
		if (ie_size) {
			if (ie_size < 0)
				rc = ie_size;
			else
				rc = -EINVAL;
			goto err_out;
		}

		buf_remainder -= ie_size;
		localbuf += ie_size;
		break;
	}
	}

err_out:
if (lfmap)
		free(lfmap);
	return rc;
}
#endif
/*
 * Validate fmaps
 */
static int
validate_simple_extlist(
	struct fmap_simple_ext *se,
	int next, int exnum, int enforce, int verbose)
{
	int i;

	for (i = 0; i < next; i++) {
		pr_verbose(verbose,
			   "        %s(%d, %d) tag=%x ofs=%ld len=%ld dev=%d\n",
			   __func__, exnum, i, se[i].struct_tag, se[i].se_offset,
			   se[i].se_len, se[i].se_devindex);

		if (enforce && se->struct_tag != LOG_SIMPLE_EXT_TAG) {
			pr_verbose(verbose, "%s(%d, %d): LOG_SIMPLE_EXT_TAG\n",
				   __func__, exnum, i);
			return -1;
		}
		if (enforce && se->se_devindex != 0) {
			pr_verbose(verbose,
				   "%s(%d, %d): non-zero se_devindex\n",
				   __func__, exnum, i);
			return -1;
		}

		/* we don't check offsets and lengths; fsck does that */
	}
	pr_verbose(verbose, "%s(%d): found %d valid simple extents\n",
		   __func__, exnum, next);
	return 0;
}

static int
validate_interleaved_extlist(
	struct fmap_mem_iext *ie,
	int next, int extnum, int enforce, int verbose)
{
	int rc;
	int i;

	for (i = 0; i < next; i++) {
		pr_verbose(verbose,
			   "    %s(%d, %d) tag=%x nstrips=%ld chunk=%ld nbytes=%d\n",
			   __func__, extnum, i, ie[i].iext.struct_tag,
			   ie[i].iext.ie_nstrips, ie[i].iext.ie_chunk_size,
			   ie[i].iext.ie_nbytes);

		if (enforce && ie[i].iext.struct_tag != LOG_IEXT_TAG) {
			pr_verbose(verbose, "%s(%d, %d): %p bad LOG_IEXT_TAG\n",
				   __func__, extnum, i, &ie[i]);
			return -1;
		}

		rc = validate_simple_extlist(ie[i].se, ie[i].iext.ie_nstrips,
					     i, enforce, verbose);
		if (rc)
			return rc;
	}
	pr_verbose(verbose, "%s(%d): found %d valid strip extents\n",
		   __func__, extnum, next);
	return 0;
}

int
validate_mem_fmap(
	struct fmap_mem_header *fm,
	int enforce,
	int verbose)
{
	int rc;
	int i;

	pr_verbose(verbose, "%s:\n", __func__);
	if (!fm)
		return -1;

	if (fm->flh.struct_tag != LOG_HEADER_TAG) {
		pr_verbose(verbose, "%s: bad LOG_HEADER_TAG\n", __func__);
		return -1;
	}

	switch (fm->flh.fmap_ext_type) {
	case FAMFS_EXT_SIMPLE:	
		pr_verbose(verbose, "%s(0): FAMFS_EXT_SIMPLE\n",
			   __func__);
		if (!fm->se) {
			pr_verbose(verbose,
				   "%s(%d): missing simple ext list\n",
				   __func__, 0);
			return -1;
		}
		rc = validate_simple_extlist(fm->se, fm->flh.next, 0,
					     enforce, verbose);

		if (rc)
			return rc;
		break;
	case FAMFS_EXT_INTERLEAVE:
		pr_verbose(verbose,
			   "%s: fmap INTERLEAVE %p: tag=%x ver=%d next=%d se=%p\n",
			   __func__, fm, fm->flh.struct_tag, fm->flh.fmap_log_version,
			   fm->flh.niext, fm->ie);
		for (i = 0; i < fm->flh.niext; i++) {
			pr_verbose(verbose, "%s(%d): FAMFS_EXT_INTERLEAVE\n",
				   __func__, i);
			if (!fm->ie) {
				pr_verbose(verbose,
				   "%s(%d): missing interleaved ext list\n",
					   __func__, i);
				return -1;
			}
			rc = validate_interleaved_extlist(fm->ie, fm->flh.niext,
							  i, enforce, verbose);
			if (rc)
				return rc;
		}
		break;
	}
	pr_verbose(verbose, "%s: good fmap\n", __func__);
	return 0;
}

#define MSG_SIZE 8192

static int
famfs_compare_simple_ext_list(
	char *msgbuf,
	int next,
	const struct famfs_simple_extent *se1,
	const struct famfs_simple_extent *se2)
{
	char *tmpbuf = calloc(1, PATH_MAX);
	int errs = 0;
	int i;

	for (i = 0; i < next; i++) {
		if (memcmp(&se1[i], &se2[i], sizeof(se1[i]))) {
			sprintf(tmpbuf, "ext %d mismatch\n", i);
			strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
			errs++;
		}
	}
	return errs;
}

int
famfs_compare_log_file_meta(
	const struct famfs_log_file_meta *m1,
	const struct famfs_log_file_meta *m2,
	int verbose)
{
	char *msgbuf = calloc(1, 8192);
	char *tmpbuf = calloc(1, PATH_MAX);
	int errs = 0;
	int j;

	if (m1->fm_size != m2->fm_size) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_size mismatch %lld / %lld",
			 m1->fm_size, m2->fm_size);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_flags != m2->fm_flags) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_flags mismatch %x / %x\n",
			 m1->fm_flags, m2->fm_flags);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_uid != m2->fm_uid) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_uid mismatch %d / %d\n",
			 m1->fm_uid, m2->fm_uid);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_gid != m2->fm_gid) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_gid mismatch %d / %d\n",
			 m1->fm_gid, m2->fm_gid);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (m1->fm_mode != m2->fm_mode) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_mode mismatch %o / %o\n",
			 m1->fm_mode, m2->fm_mode);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}
	if (strcmp(m1->fm_relpath, m2->fm_relpath) != 0) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_relpath mismatch %s / %s\n",
			 m1->fm_relpath, m2->fm_relpath);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
	}

	if (verbose && errs) {
		fprintf(stderr, msgbuf);
		msgbuf[0] = 0;
	}

	if (m1->fm_fmap.fmap_ext_type != m2->fm_fmap.fmap_ext_type) {
		snprintf(tmpbuf, PATH_MAX - 1, "fm_ext_type mismatch %d / %d\n",
			 m1->fm_fmap.fmap_ext_type, m2->fm_fmap.fmap_ext_type);
		strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
		errs++;
		return errs;
	}

	switch (m1->fm_fmap.fmap_ext_type) {
	case FAMFS_EXT_SIMPLE:
		if (m1->fm_fmap.fmap_nextents != m1->fm_fmap.fmap_nextents) {
			snprintf(tmpbuf, PATH_MAX - 1, "fm_ext_type mismatch %d / %d\n",
				 m1->fm_fmap.fmap_nextents, m2->fm_fmap.fmap_nextents);
			strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
			errs++;
			goto out;
		}
		errs += famfs_compare_simple_ext_list(msgbuf, m1->fm_fmap.fmap_nextents,
						      m1->fm_fmap.se, m2->fm_fmap.se);
		break;
	case FAMFS_EXT_INTERLEAVE:
		if (m1->fm_fmap.fmap_niext != m1->fm_fmap.fmap_niext) {
			snprintf(tmpbuf, PATH_MAX - 1, "fmap_niext mismatch %d / %d\n",
				 m1->fm_fmap.fmap_niext, m2->fm_fmap.fmap_niext);
			strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
			errs++;
			goto out;
		}
		for (j = 0; j < m1->fm_fmap.fmap_niext; j++) {
			if (m1->fm_fmap.ie[j].ie_nstrips != m2->fm_fmap.ie[j].ie_nstrips) {
				snprintf(tmpbuf, PATH_MAX - 1,
					 "ie[%d].nstrips mismatch %lld / %lld\n", j,
					 m1->fm_fmap.ie[j].ie_nstrips,
					 m2->fm_fmap.ie[j].ie_nstrips);
				strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
				errs++;
			}
			if (m1->fm_fmap.ie[j].ie_chunk_size != m2->fm_fmap.ie[j].ie_chunk_size) {
				snprintf(tmpbuf, PATH_MAX - 1,
					 "ie[%d].chunk_size mismatch %lld / %lld\n", j,
					 m1->fm_fmap.ie[j].ie_chunk_size,
					 m2->fm_fmap.ie[j].ie_chunk_size);
				strncat(msgbuf, tmpbuf, MSG_SIZE - 1);
				errs++;
			}
			errs += famfs_compare_simple_ext_list(msgbuf, m1->fm_fmap.fmap_niext,
							      m1->fm_fmap.ie[j].ie_strips,
							      m2->fm_fmap.ie[j].ie_strips);
		}
	}

out:
	if (verbose && errs) {
		fprintf(stderr, msgbuf);
		msgbuf[0] = 0;
	}

	return errs;
}
