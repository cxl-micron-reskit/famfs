// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2024-2025 Micron Technology, Inc.  All rights reserved.
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
	uint msg_size,
	int file_type,
	const struct famfs_log_file_meta *fmeta)
{
	struct fuse_famfs_fmap_header *flh = (struct fuse_famfs_fmap_header *)msg;
	const struct famfs_log_fmap *log_fmap = &fmeta->fm_fmap;
	uint cursor = 0;
	uint i, j;

	if (msg_size < sizeof(*flh))
		return -EINVAL;

	flh->fmap_version = FAMFS_FMAP_VERSION;
	flh->file_type = file_type;
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
		return -EINVAL;
	}

	return cursor;

err_out:
	return -EINVAL;
}

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
