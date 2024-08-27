// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
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
#include <linux/famfs_ioctl.h>

#include "famfs_meta.h"
#include "famfs_lib.h"
#include "famfs_lib_internal.h"
#include "bitmap.h"
#include "mu_mem.h"


void
mu_print_bitmap(u8 *bitmap, int num_bits)
{
	int i, val;
	int sum = 0;
	char linebuf[256] = {0};

	mu_bitmap_foreach(bitmap, num_bits, i, val) {
		sum += val;

		if (!(i%64)) {
			/* New line; print previous line only is there was at least one '1' in it */
			if (sum > 0) {
				printf("%s", linebuf);
				sum = 0;
			}

			/* Start over with next line */
			linebuf[0] = 0;
			sprintf(linebuf, "\n%4d: ", i); /* Put header in line */
		}

		strcat(linebuf, (val) ? "1" : "0");     /* Append a '1' or '0' */
	}
	if (sum > 0)
		printf("%s", linebuf);
	printf("\n");
}

/**
 * set_extent_in_bitmap() - Set bits for an allocation range
 */
static inline u64
set_extent_in_bitmap(
	u8 *bitmap,
	u64 offset,
	u64 len,
	u64 *alloc_sum)
{
	u64 errors = 0;
	u64 page_num;
	u64 np;
	int rc;
	u64 k;

	assert(!(offset & (FAMFS_ALLOC_UNIT  - 1)));

	page_num = offset / FAMFS_ALLOC_UNIT;
	np = (len + FAMFS_ALLOC_UNIT - 1) / FAMFS_ALLOC_UNIT;

	for (k = page_num; k < (page_num + np); k++) {
		rc = mu_bitmap_test_and_set(bitmap, k);
		if (rc == 0) {
			errors++; /* bit was already set */
		} else {
			/* Don't count double allocations */
			if (alloc_sum)
				*alloc_sum += FAMFS_ALLOC_UNIT;
		}
	}
	return errors;
}

/**
 * put_sb_log_into_bitmap()
 *
 * The two files that are not in the log are the superblock and the log. So these
 * files need to be manually added to the allocation bitmap. This function does that.
 *
 * @bitmap:    The bitmap
 * @log_len:   Size of the log (superblock size is invariant)
 * @alloc_sum: Amount of space marked as allocated for  the superblock and log
 */
static inline void
put_sb_log_into_bitmap(u8 *bitmap, u64 log_len, u64 *alloc_sum)
{
	set_extent_in_bitmap(bitmap, 0, FAMFS_SUPERBLOCK_SIZE + log_len, alloc_sum);
}

/**
 * famfs_build_bitmap()
 *
 * XXX: this is only aware of the first daxdev in the superblock's list
 * @logp:
 * @size_in:          total size of allocation space in bytes
 * @bitmap_nbits_out: output: size of the bitmap
 * @alloc_errors_out: output: number of times a file referenced a bit that was already set
 * @fsize_total_out:  output: if ptr non-null, this is the sum of the file sizes
 * @alloc_sum_out:    output: if ptr non-null, this is the sum of all allocation sizes
 *                    (excluding double-allocations; space amplification is
 *                     @alloc_sum / @size_total provided there are no double allocations,
 *                     b/c those will increase size_total but not alloc_sum)
 * @log_stats_out:    Optional pointer to struct log_stats to be copied out
 * @verbose:
 */
u8 *
famfs_build_bitmap(const struct famfs_log   *logp,
		   u64                       dev_size_in,
		   u64                      *bitmap_nbits_out,
		   u64                      *alloc_errors_out,
		   u64                      *fsize_total_out,
		   u64                      *alloc_sum_out,
		   struct famfs_log_stats   *log_stats_out,
		   int                       verbose)
{
	u64 nbits = (dev_size_in + FAMFS_ALLOC_UNIT - 1) / FAMFS_ALLOC_UNIT;
	u64 bitmap_nbytes = mu_bitmap_size(nbits);
	u8 *bitmap = calloc(1, bitmap_nbytes + 1); /* Note: mu_bitmap_foreach accesses
						    * 1 bit past the end */
	struct famfs_log_stats ls = { 0 }; /* We collect a subset of stats collected by logplay */
	u64 errors = 0;
	u64 alloc_sum = 0;
	u64 fsize_sum  = 0;
	u64 i, j;

	if (verbose > 1)
		printf("%s: dev_size %lld nbits %lld bitmap_nbytes %lld\n",
		       __func__, dev_size_in, nbits, bitmap_nbytes);

	if (!bitmap)
		return NULL;

	put_sb_log_into_bitmap(bitmap, logp->famfs_log_len, &alloc_sum);

	/* This loop is over all log entries */
	for (i = 0; i < logp->famfs_log_next_index; i++) {
		const struct famfs_log_entry *le = &logp->entries[i];

		ls.n_entries++;

		/* TODO: validate log sequence number */

		switch (le->famfs_log_entry_type) {
		case FAMFS_LOG_FILE: {
			const struct famfs_file_meta *fm = &le->famfs_fm;
			const struct famfs_log_fmap *ext = &fm->fm_fmap;

			ls.f_logged++;
			fsize_sum += fm->fm_size;
			if (verbose > 1)
				printf("%s: file=%s size=%lld\n", __func__,
				       fm->fm_relpath, fm->fm_size);

			/* For each extent in this log entry, mark the bitmap as allocated */
			for (j = 0; j < fm->fm_fmap.fmap_nextents; j++) {
				u64 ofs = ext->se[j].se_offset;
				u64 len = ext->se[j].se_len;
				int rc;

				assert(!(ofs % FAMFS_ALLOC_UNIT));

				rc = set_extent_in_bitmap(bitmap, ofs, len, &alloc_sum);
				errors += rc;
			}
			break;
		}
		case FAMFS_LOG_MKDIR:
			ls.d_logged++;
			/* Ignore directory log entries - no space is used */
			break;

		default:
			printf("%s: invalid log entry\n", __func__);
			break;
		}
	}
	if (verbose > 1) {
		mu_print_bitmap(bitmap, nbits);
	}
	if (bitmap_nbits_out)
		*bitmap_nbits_out = nbits;
	if (alloc_errors_out)
		*alloc_errors_out = errors;
	if (fsize_total_out)
		*fsize_total_out = fsize_sum;
	if (alloc_sum_out)
		*alloc_sum_out = alloc_sum;
	if (log_stats_out)
		memcpy(log_stats_out, &ls, sizeof(ls));
	return bitmap;
}

/**
 * bitmap_alloc_contiguous()
 *
 * @bitmap:
 * @nbits:       number of bits in the bitmap
 * @alloc_size:  size to allocate in bytes (must convert to bits)
 * @cur_pos:     Starting offset to search from
 * @range_size:  size (bytes) of range to allocate from (starting at @cur_pos)
 *               (zero maens alloc from the whole bitmap)
 *               (this is used for strided/striped allocations)
 *
 * Return value: the offset in bytes
 */
static s64
bitmap_alloc_contiguous(
	u8 *bitmap,
	u64 nbits,
	u64 alloc_size,
	u64 *cur_pos,
	u64 range_size)
{
	u64 i, j;
	u64 alloc_bits = (alloc_size + FAMFS_ALLOC_UNIT - 1) /  FAMFS_ALLOC_UNIT;
	u64 bitmap_remainder;
	u64 start_idx;
	u64 range_size_nbits;

	assert(cur_pos);

	start_idx = *cur_pos / FAMFS_ALLOC_UNIT;
	range_size_nbits = (range_size) ?
		((range_size + FAMFS_ALLOC_UNIT - 1) / FAMFS_ALLOC_UNIT) : nbits;

	for (i = start_idx; i < nbits; i++) {
		/* Skip bits that are set... */
		if (mu_bitmap_test(bitmap, i))
			continue;

		bitmap_remainder = start_idx + range_size_nbits - i;
		if (alloc_bits > bitmap_remainder) /* Remaining space is not enough */
			return -1;

		for (j = i; j < (i+alloc_bits); j++) {
			if (mse_bitmap_test32(bitmap, j))
				goto next;
		}
		/* If we get here, we didn't hit the "continue" which means that bits
		 * i-(i+alloc_bits) are available
		 */
		for (j = i; j < (i+alloc_bits); j++)
			mse_bitmap_set32(bitmap, j);
		*cur_pos = j * FAMFS_ALLOC_UNIT;
		return i * FAMFS_ALLOC_UNIT;
next:
		continue;
	}
	fprintf(stderr, "%s: alloc failed\n", __func__);
	return -1;
}

static void
bitmap_free_contiguous(
	u8 *bitmap,
	u64 nbits,
	u64 offset,
	u64 len)
{
	u64 start_bitnum = (offset / FAMFS_ALLOC_UNIT);
	u64 nbits_free = (len + FAMFS_ALLOC_UNIT - 1) / FAMFS_ALLOC_UNIT;
	u64 i;

	assert((start_bitnum + nbits_free) <= nbits);
	assert(!(offset % FAMFS_ALLOC_UNIT));

	for (i = start_bitnum; i < (start_bitnum + nbits_free); i++)
		assert(mu_bitmap_test_and_clear(bitmap, i)); /* Stop if any bits are aleady clear */
}

/**
 * famfs_alloc_contiguous()
 *
 * @lp:      locked log struct. Will perform bitmap build if no already done
 * @size:    Size to allocate
 * @range_size: fail the allocation if it can't be met within this size of the starting point
 * @verbose:
 */
static s64
famfs_alloc_contiguous(
	struct famfs_locked_log *lp,
	u64 size,
	u64 range_size,
	int verbose)
{
	if (!lp->bitmap) {
		/* Bitmap is needed and hasn't been built yet */
		lp->bitmap = famfs_build_bitmap(lp->logp, lp->devsize, &lp->nbits,
						NULL, NULL, NULL, NULL, verbose);
		if (!lp->bitmap) {
			fprintf(stderr, "%s: failed to allocate bitmap\n", __func__);
			return -1;
		}
		lp->cur_pos = 0;
	}
	return bitmap_alloc_contiguous(lp->bitmap, lp->nbits, size, &lp->cur_pos, range_size);
}

/**
 * famfs_file_alloc_contiguous()
 *
 * Alllocate space for a file, making it ready to use
 *
 * Caller has already done the following:
 * * Verify that master role via the superblock
 * * Verify that creating this file is legit (does not already exist etc.)
 *
 * @lp:       Struct famfs_locked_log or NULL
 * @size:     size to alloacte
 * @fmap_out: Allocated extent list (if rc==0)
 *            (caller is responsible for freeing this list)
 * @verbose:
 *
 * Returns 0 on success
 * On error, returns:
 * >0 - Errors that should not abort a multi-file operation
 * <0 - Errors that should cause an abort (such as out of space or log full)
 */
int
famfs_file_alloc_contiguous(
	struct famfs_locked_log     *lp,
	u64                          size,
	struct famfs_log_fmap      **fmap_out,
	int                          verbose)
{
	struct famfs_log_fmap *fmap = calloc(1, sizeof(*fmap));
	s64 offset;
	int rc = 0;

	assert(fmap_out);
	assert(fmap);

	offset = famfs_alloc_contiguous(lp, size, 0 /* No range limit */,
					verbose);
	if (offset < 0) {
		rc = -ENOMEM;
		fprintf(stderr, "%s: Out of space!\n", __func__);
		goto out;
	}

	/* Allocation at offset 0 is always wrong - the superblock lives there */
	assert(offset != 0);

	fmap->fmap_ext_type = FAMFS_EXT_SIMPLE;
	fmap->se[0].se_devindex = 0;
	fmap->se[0].se_len = round_size_to_alloc_unit(size);
	fmap->se[0].se_offset = offset;
	fmap->fmap_nextents = 1;

	*fmap_out = fmap;

out:
	return rc;
}

/*******************************************************************************
 * Strided allocator stuff
 */

#define BUCKET_SERIES_MAX 64

struct bucket_series {
	int nbuckets;
	int current;
	int buckets[BUCKET_SERIES_MAX];
};

static void
init_bucket_series(
	struct bucket_series *bs,
	int nbuckets)
{
	int i;

	assert(bs);
	bs->nbuckets = nbuckets;

	for (int i = 0; i < nbuckets; i++) {
		bs->buckets[i] = i;
	}

	/* Randomize the order of the bucket values */
	srand(time(NULL));
	for (i = nbuckets - 1; i > 0; i--) {
		int j = rand() % (i + 1);
		int tmp;

		tmp = bs->buckets[i];
		bs->buckets[i] = bs->buckets[j];
		bs->buckets[j] = tmp;
	}

	bs->current = 0;
}

static int next_bucket(struct bucket_series *bs)
{
	if (bs->current >= bs->nbuckets)
		return -1;

	return(bs->buckets[bs->current++]);
}

int
famfs_validate_stripe(
	struct famfs_stripe *stripe,
	u64 devsize,
	int verbose)
{
	u64 bucket_size;
	//int total_strips;
	int errs = 0;

	if (!stripe->nbuckets && !stripe->nstrips && !stripe->chunk_size)
		return 0; /* All 0's is valid */

	assert(devsize);
	if (!stripe->chunk_size) {
		errs++;
		if (verbose)
			fprintf(stderr, "%s: Error NULL chunk_size\n", __func__);
	}
	if (stripe->chunk_size % FAMFS_ALLOC_UNIT) {
		errs++;
		if (verbose)
			fprintf(stderr,
				"%s: Error chunk_size %lld no ta multiplle of alloc_unit (%d)\n",
				__func__, stripe->chunk_size, FAMFS_ALLOC_UNIT);
	}
	if (!stripe->nbuckets) {
		errs++;
		if (verbose)
			fprintf(stderr, "%s: Error NULL nbuckets\n", __func__);
	}
	if (stripe->nstrips > stripe->nbuckets) {
		errs++;
		if (verbose)
			fprintf(stderr, "%s: Error nstrips (%lld) > nbuckets (%lld)\n",
				__func__, stripe->nstrips, stripe->nbuckets);
	}

	bucket_size = (stripe->nbuckets) ? devsize / stripe->nbuckets : 0;
	if (bucket_size < 1024 * 1024 * 1024) {
		errs++;
		if (verbose)
			fprintf(stderr, "%s: Bucket_size (%lld) < 1G\n", __func__, bucket_size);
	}

	return errs;
}

static int
famfs_file_strided_alloc(
	struct famfs_locked_log *lp,
	u64 size,
	struct famfs_log_fmap **fmap_out,
	int verbose)
{
	struct famfs_simple_extent *strips;
	struct famfs_log_fmap *fmap;
	struct bucket_series bs = { 0 };
	u64 nstrips_allocated = 0;
	int nstripes;
	u64 tmp;
	/* Quantities in allocation units (au) */
	u64 alloc_size_au, devsize_au, bucket_size_au, stripe_size_au, strip_size_au, chunk_size_au;
	int i, j;

	assert(lp->stripe.nbuckets && lp->stripe.nstrips);
	assert(lp->stripe.nstrips <= lp->stripe.nbuckets);

	if (lp->stripe.chunk_size & (lp->stripe.chunk_size - 1)) {
		fprintf(stderr, "%s: chunk_size=0x%llx must be a power of 2\n",
			__func__, lp->stripe.chunk_size);
		return -EINVAL;
	}
	if (size < lp->stripe.chunk_size) {
		/* if the file size is less than a chunk, fall back to a contiguous allocation
		 * from a random bucket
		 */

		lp->cur_pos = 0;
		return famfs_file_alloc_contiguous(lp, size, fmap_out, verbose);
	}
	if (lp->stripe.chunk_size % FAMFS_ALLOC_UNIT) {
		fprintf(stderr, "%s: chunk_size=0x%llx must be a multiple of FAMFS_ALLOC_UNIT\n",
			__func__, lp->stripe.chunk_size);
		return -EINVAL;
	}


	alloc_size_au = (size + FAMFS_ALLOC_UNIT - 1) / FAMFS_ALLOC_UNIT;

	/* Authoritative device, bucket, stripe and strip sizes are in allocation units */
	chunk_size_au  = lp->stripe.chunk_size / FAMFS_ALLOC_UNIT;
	devsize_au     = lp->devsize / FAMFS_ALLOC_UNIT;    /* This may round down */
	bucket_size_au = devsize_au / lp->stripe.nbuckets;         /* This may also round down */
	stripe_size_au = lp->stripe.nstrips * chunk_size_au;
	assert(!(stripe_size_au % lp->stripe.nstrips));

	nstripes = (alloc_size_au + stripe_size_au - 1) / stripe_size_au;
	strip_size_au  = nstripes * chunk_size_au;

	/* Just for curiosity, let's check how much space at the end of devsize is leftover
	 * after the last bucket */
	tmp = bucket_size_au * FAMFS_ALLOC_UNIT * lp->stripe.nbuckets;
	assert(tmp <= lp->devsize);
	if (verbose && tmp < lp->devsize)
		printf("%s: nbuckets=%lld wastes %lld bytes of dev capacity\n",
		       __func__, lp->stripe.nbuckets, lp->devsize - tmp);

	if (verbose > 1)
		printf("%s: size=0x%llx stripe_size=0x%llx strip_size=0x%llx\n",
		       __func__, size,
		       stripe_size_au * FAMFS_ALLOC_UNIT,
		       strip_size_au * FAMFS_ALLOC_UNIT);

	/* Bucketize the stride regions in random order */
	init_bucket_series(&bs, lp->stripe.nbuckets);

	fmap = calloc(1, sizeof(*fmap));
	if (!fmap)
		return -ENOMEM;

	fmap->fmap_ext_type = FAMFS_EXT_INTERLEAVE;

	/* We currently only support one striped extent - hence index [0] */
	fmap->ie[0].ie_nstrips = lp->stripe.nstrips;
	fmap->ie[0].ie_chunk_size = lp->stripe.chunk_size;
	fmap->ie[0].ie_nstrips = lp->stripe.nstrips;
	strips = fmap->ie[0].ie_strips;

	/* Allocate our strips. If nstrips is <  nbuckets, we can tolerate some failures */
	for (i = 0; i < lp->stripe.nbuckets; i++) {
		int bucket_num = next_bucket(&bs);
		s64 ofs;
		u64 pos = bucket_num * bucket_size_au * FAMFS_ALLOC_UNIT;

		/* Oops: bitmap might not be allocated yet */
		ofs = bitmap_alloc_contiguous(lp->bitmap, lp->nbits,
					      strip_size_au * FAMFS_ALLOC_UNIT,
					      &pos,
					      bucket_size_au * FAMFS_ALLOC_UNIT);

		if (ofs > 0) {

			strips[nstrips_allocated].se_devindex = 0;
			strips[nstrips_allocated].se_offset = ofs;
			strips[nstrips_allocated].se_len = strip_size_au * FAMFS_ALLOC_UNIT;

			if (verbose)
				printf("%s: strip %d bucket %d ofs 0x%llx len %lld\n",
				       __func__, i, bucket_num, ofs,
				       strip_size_au * FAMFS_ALLOC_UNIT);
			nstrips_allocated++;
		}
	}

	if (nstrips_allocated < lp->stripe.nstrips) {
		/* Allocation failed; got fewer strips than needed */
		fprintf(stderr, "%s: failed %lld strips @%lldb each; got %lld strips\n",
			__func__, lp->stripe.nstrips, strip_size_au * FAMFS_ALLOC_UNIT,
			nstrips_allocated);

		if (verbose > 1) {
			printf("%s: before freeing strips from failed alloc:", __func__);
			mu_print_bitmap(lp->bitmap, lp->nbits);
		}

		for (j = 0; j < i; j++)
			bitmap_free_contiguous(lp->bitmap, lp->nbits,
					       strips[j].se_offset, strips[j].se_len);
		free(fmap);
		if (verbose > 1) {
			printf("%s: after:", __func__);
			mu_print_bitmap(lp->bitmap, lp->nbits);
		}

		return -ENOMEM;
	}
	fmap->fmap_nextents = 1;
	*fmap_out = fmap;

	if (verbose > 1)
		mu_print_bitmap(lp->bitmap, lp->nbits);

	return 0;
}

int
famfs_file_alloc(
	struct famfs_locked_log     *lp,
	u64                          size,
	struct famfs_log_fmap      **fmap_out,
	int                          verbose)
{
	if ((FAMFS_KABI_VERSION <= 42) || (!lp->stripe.nbuckets || !lp->stripe.nstrips))
		return famfs_file_alloc_contiguous(lp, size, fmap_out, verbose);

	return famfs_file_strided_alloc(lp, size, fmap_out, verbose);
}

