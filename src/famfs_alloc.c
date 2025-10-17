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
			/* New line; print previous line only if
			 * there was at least one '1' in it */
			if (sum > 0) {
				printf("%s", linebuf);
				sum = 0;
			}

			/* Start over with next line */
			linebuf[0] = 0;
			sprintf(linebuf, "\n%4d: ", i); /* Put header in line */
		}

		strcat(linebuf, (val) ? "1" : "0");   /* Append a '1' or '0' */
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
	const u64 alloc_unit,
	u64 offset,
	u64 len,
	u64 *alloc_sum)
{
	u64 errors = 0;
	u64 page_num;
	u64 np;
	int rc;
	u64 k;

	assert(!(offset & (alloc_unit  - 1)));

	page_num = offset / alloc_unit;
	np = (len + alloc_unit - 1) / alloc_unit;

	for (k = page_num; k < (page_num + np); k++) {
		rc = mu_bitmap_test_and_set(bitmap, k);
		if (rc == 0) {
			errors++; /* bit was already set */
		} else {
			/* Don't count double allocations */
			if (alloc_sum)
				*alloc_sum += alloc_unit;
		}
	}
	return errors;
}

/**
 * put_sb_log_into_bitmap()
 *
 * The two files that are not in the log are the superblock and the log.
 * So these files need to be manually added to the allocation bitmap. This
 * function does that.
 *
 * @bitmap:    The bitmap
 * @log_len:   Size of the log (superblock size is invariant)
 * @alloc_sum: Amount of space marked as allocated for  the superblock and log
 */
static inline void
put_sb_log_into_bitmap(
	u8 *bitmap,
	const u64 alloc_unit,
	u64 log_len,
	u64 *alloc_sum)
{
	set_extent_in_bitmap(bitmap, alloc_unit, 0,
			     FAMFS_SUPERBLOCK_SIZE + log_len, alloc_sum);
}

/**
 * famfs_build_bitmap()
 *
 * @logp:
 * @size_in:          total size of allocation space in bytes
 * @bitmap_nbits_out: output: size of the bitmap
 * @alloc_errors_out: output: number of times a file referenced a bit that was
 *                    already set
 * @fsize_total_out:  output: if ptr non-null, this is the sum of the file sizes
 * @alloc_sum_out:    output: if ptr non-null, this is the sum of all
 *                    allocation sizes
 *                    (excluding double-allocations; space amplification is
 *                    @alloc_sum / @size_total provided there are no double
 *                    allocations, b/c those will increase size_total but not
 *                    alloc_sum)
 * @log_stats_out:    Optional pointer to struct log_stats to be copied out
 * @verbose:
 */
u8 *
famfs_build_bitmap(const struct famfs_log   *logp,
		   const u64                 alloc_unit,
		   u64                       dev_size_in,
		   u64                      *bitmap_nbits_out,
		   u64                      *alloc_errors_out,
		   u64                      *fsize_total_out,
		   u64                      *alloc_sum_out,
		   struct famfs_log_stats   *log_stats_out,
		   int                       verbose)
{
	u64 bitmap_nbytes;
	u8 *bitmap;
	u64 nbits;

	struct famfs_log_stats ls = { 0 }; /* We collect a subset of stats
					    * collected by logplay */
	u64 fsize_sum  = 0;
	u64 alloc_sum = 0;
	u64 errors = 0;
	u64 i, j;

	assert (alloc_unit);
	assert((alloc_unit & (alloc_unit - 1)) == 0);

	nbits = (dev_size_in + alloc_unit - 1) / alloc_unit;
	bitmap_nbytes = mu_bitmap_size(nbits);
	bitmap = calloc(1, bitmap_nbytes + 1); /* Note: mu_bitmap_foreach
						* accesses 1 bit past
						* the end */

	if (verbose > 1)
		printf("%s: dev_size %lld nbits %lld bitmap_nbytes %lld\n",
		       __func__, dev_size_in, nbits, bitmap_nbytes);

	if (!bitmap)
		return NULL;

	put_sb_log_into_bitmap(bitmap, alloc_unit, logp->famfs_log_len,
			       &alloc_sum);

	/* This loop is over all log entries */
	for (i = 0; i < logp->famfs_log_next_index; i++) {
		const struct famfs_log_entry *le = &logp->entries[i];

		ls.n_entries++;

		if (famfs_validate_log_entry(le, i)) {
			ls.bad_entries++;
			continue;
		}

		switch (le->famfs_log_entry_type) {
		case FAMFS_LOG_FILE: {
			const struct famfs_log_file_meta *fm = &le->famfs_fm;
			const struct famfs_log_fmap *fmap = &fm->fm_fmap;
			const struct famfs_log_fmap *ext = &fm->fm_fmap;
				
			ls.f_logged++;
			fsize_sum += fm->fm_size;

			switch (fmap->fmap_ext_type) {
			case FAMFS_EXT_SIMPLE:
				if (verbose > 1)
					printf("%s: file=%s size=%lld\n",
					       __func__,
					       fm->fm_relpath, fm->fm_size);

				/* For each extent in this log entry,
				 * mark the bitmap as allocated */
				for (j = 0; j < fmap->fmap_nextents; j++) {
					u64 ofs = ext->se[j].se_offset;
					u64 len = ext->se[j].se_len;
					int rc;

					assert(!(ofs % alloc_unit));

					rc = set_extent_in_bitmap(bitmap,
								  alloc_unit,
								  ofs, len,
								  &alloc_sum);
					errors += rc;
				}
				break;
			case FAMFS_EXT_INTERLEAVE: {
				int nstripes = fmap->fmap_niext;
				int j;
				u64 k;

				for (j = 0; j < nstripes; j++) {
					const struct famfs_interleaved_ext *stripes = &fmap->ie[j];

					for (k = 0; k < stripes[j].ie_nstrips;
					     k++) {
						const struct famfs_simple_extent *se =
							&(stripes[j].ie_strips[k]);
						u64 ofs = se->se_offset;
						u64 len = se->se_len;
						int rc;

						rc = set_extent_in_bitmap(bitmap, alloc_unit,
									  ofs, len, &alloc_sum);
						errors += rc;
					}
				}
				break;
			}
			default:
				fprintf(stderr,
					"%s: entry %lld of %lld: "
					"bad fmap_ext_type %d\n",
					__func__, i, logp->famfs_log_next_index,
					fmap->fmap_ext_type);
			}
		}
		  break;
		case FAMFS_LOG_MKDIR:
			ls.d_logged++;
			/* Ignore directory log entries - no space is used */
			break;

		default:
			fprintf(stderr,
				"%s: log entry %lld of %lld: bad type (%d)\n",
				__func__, i, logp->famfs_log_next_index,
				le->famfs_log_entry_type);
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
	const u64 alloc_unit,
	u64 alloc_size,
	u64 *cur_pos,
	u64 range_size)
{
	u64 i, j;
	u64 alloc_bits = (alloc_size + alloc_unit - 1) /  alloc_unit;
	u64 bitmap_remainder;
	u64 start_idx;
	u64 range_size_nbits;

	assert(cur_pos);

	start_idx = *cur_pos / alloc_unit;
	range_size_nbits = (range_size) ?
		((range_size + alloc_unit - 1) / alloc_unit) : nbits;

	for (i = start_idx; i < nbits; i++) {
		/* Skip bits that are set... */
		if (mu_bitmap_test(bitmap, i))
			continue;

		bitmap_remainder = start_idx + range_size_nbits - i;
		if (alloc_bits > bitmap_remainder) /* Remaining space is not enough */
			return -1;

		for (j = i; j < (i+alloc_bits); j++) {
			if (mu_bitmap_test(bitmap, j))
				goto next;
		}
		/* If we get here, we didn't hit the "continue" which means that bits
		 * i-(i+alloc_bits) are available
		 */
		for (j = i; j < (i+alloc_bits); j++)
			mse_bitmap_set32(bitmap, j);
		*cur_pos = j * alloc_unit;
		return i * alloc_unit;
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
	const u64 alloc_unit,
	u64 offset,
	u64 len)
{
	u64 start_bitnum = (offset / alloc_unit);
	u64 nbits_free = (len + alloc_unit - 1) / alloc_unit;
	u64 i;

	assert((start_bitnum + nbits_free) <= nbits);
	assert(!(offset % alloc_unit));

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
	u64 range_size)
{
	return bitmap_alloc_contiguous(lp->bitmap, lp->nbits, lp->alloc_unit,
				       size, &lp->cur_pos, range_size);
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
	struct famfs_log_fmap      **fmap_out)
{
	struct famfs_log_fmap *fmap = calloc(1, sizeof(*fmap));
	s64 offset;
	int rc = 0;

	assert(fmap_out);
	assert(fmap);

	offset = famfs_alloc_contiguous(lp, size, 0 /* No range limit */);
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

void bucket_series_destroy(struct bucket_series *bs)
{
	assert(bs);
	if (bs->buckets)
		free(bs->buckets);
	free(bs);
}

void
bucket_series_alloc(
	struct bucket_series **bs,
	u64 nbuckets,
	u64 start)
{
	struct bucket_series *local_bs;
	static int seeded = 0;
	int i;

	assert(bs);
	local_bs = calloc(1, sizeof(*local_bs));
	assert(local_bs);

	local_bs->buckets = calloc(nbuckets, sizeof(*local_bs->buckets));
	assert(local_bs->buckets);
	local_bs->nbuckets = nbuckets;

	for (u64 i = 0; i < nbuckets; i++) {
		local_bs->buckets[i] = i + start;
	}

	/* Seed the random generator only once */
	if (!seeded) {
		seeded++;
		srand(time(NULL));
	}

	/* Randomize the order of the bucket values
	 * (the old Fisher-Yates/Knuth shuffle) */
	for (i = nbuckets - 1; i > 0; i--) {
		u64 j = rand() % (i + 1);
		int tmp;

		tmp = local_bs->buckets[i];
		local_bs->buckets[i] = local_bs->buckets[j];
		local_bs->buckets[j] = tmp;
	}

	local_bs->current = 0;
	*bs = local_bs;
}

s64 bucket_series_next(struct bucket_series *bs)
{
	if (bs->current >= bs->nbuckets)
		return -1;

	return(bs->buckets[bs->current++]);
}

void bucket_series_rewind(struct bucket_series *bs)
{
	bs->current = 0;
}

int
famfs_validate_interleave_param(
	struct famfs_interleave_param *interleave_param,
	const u64 alloc_unit,
	u64 devsize,
	int verbose)
{
	extern int mock_stripe;
	u64 bucket_size;
	int errs = 0;

	assert(interleave_param);
	assert(devsize);

	if (!interleave_param->nbuckets
	    && !interleave_param->nstrips
	    && !interleave_param->chunk_size)
		return 0; /* All 0's is valid */

	if (!interleave_param->chunk_size) {
		errs++;
		if (verbose)
			fprintf(stderr, "%s: Error NULL chunk_size\n", __func__);
	}
	if (interleave_param->chunk_size & (interleave_param->chunk_size - 1)) {
		fprintf(stderr, "%s: chunk_size=0x%llx must be a power of 2\n",
			__func__, interleave_param->chunk_size);
		return -EINVAL;
	}
	if (interleave_param->chunk_size % alloc_unit) {
		fprintf(stderr,
		     "%s: chunk_size=0x%llx must be a multiple of alloc_unit\n",
			__func__, interleave_param->chunk_size);
		return -EINVAL;
	}
	if (interleave_param->nstrips > interleave_param->nbuckets) {
		errs++;
		if (verbose)
			fprintf(stderr,
				"%s: Error nstrips (%lld) > nbuckets (%lld)\n",
				__func__, interleave_param->nstrips,
				interleave_param->nbuckets);
	}
	if (!interleave_param->nbuckets) {
		errs++;
		if (verbose)
			fprintf(stderr, "%s: Error NULL nbuckets\n", __func__);
	}
	if (interleave_param->nbuckets > FAMFS_MAX_NBUCKETS) {
		errs++;
		if (verbose)
			fprintf(stderr,
				"%s: Error nbuckets %lld exceeds max %d\n",
				__func__, interleave_param->nbuckets,
				FAMFS_MAX_NBUCKETS);
	}

	bucket_size = (interleave_param->nbuckets) ? devsize / interleave_param->nbuckets : 0;
	if ((bucket_size < 1024 * 1024 * 1024) && !mock_stripe) {
		errs++;
		if (verbose)
			fprintf(stderr, "%s: Bucket_size (%lld) < 1G\n",
				__func__, bucket_size);
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
	struct bucket_series *bs = NULL;
	u64 nstrips_allocated = 0;
	int nstripes;
	s64 tmp;
	/* Quantities in units of alloc_unit (au) */
	u64 alloc_size_au, devsize_au, bucket_size_au;
	u64 stripe_size_au, strip_size_au, chunk_size_au;
	u64 i, j;
	int rc;

	rc = famfs_validate_interleave_param(&lp->interleave_param,
					     lp->alloc_unit,
					     lp->devsize, verbose);
	if (rc)
		return rc;
	assert(lp->interleave_param.nstrips <= lp->interleave_param.nbuckets);

	if (size < lp->interleave_param.chunk_size) {
		/* if the file size is less than a chunk, fall back to a
		 * contiguous allocation from a random bucket
		 */

		lp->cur_pos = 0;
		return famfs_file_alloc_contiguous(lp, size, fmap_out);
	}

	alloc_size_au = (size + lp->alloc_unit - 1) / lp->alloc_unit;

	/* Authoritative device, bucket, stripe and strip sizes are in
	 * allocation units */
	chunk_size_au  = lp->interleave_param.chunk_size / lp->alloc_unit;
	/* This may round down: */
	devsize_au     = lp->devsize / lp->alloc_unit;
	/* This may also round down: */
	bucket_size_au = devsize_au / lp->interleave_param.nbuckets;
	stripe_size_au = lp->interleave_param.nstrips * chunk_size_au;
	assert(!(stripe_size_au % lp->interleave_param.nstrips));

	nstripes = (alloc_size_au + stripe_size_au - 1) / stripe_size_au;
	strip_size_au  = nstripes * chunk_size_au;

	/* Just for curiosity, let's check how much space at the end of
	 * devsize is leftover after the last bucket */
	tmp = bucket_size_au * lp->alloc_unit * lp->interleave_param.nbuckets;
	assert(tmp <= lp->devsize);
	if (verbose && tmp < lp->devsize)
		printf("%s: nbuckets=%lld wastes %lld bytes of dev capacity\n",
		       __func__, lp->interleave_param.nbuckets,
		       lp->devsize - tmp);

	if (verbose > 1)
		printf("%s: size=0x%llx stripe_size=0x%llx strip_size=0x%llx\n",
		       __func__, size,
		       stripe_size_au * lp->alloc_unit,
		       strip_size_au * lp->alloc_unit);

	fmap = calloc(1, sizeof(*fmap));
	if (!fmap)
		return -ENOMEM;

	/* Bucketize the stride regions in random order */
	bucket_series_alloc(&bs, lp->interleave_param.nbuckets, 0);

	fmap->fmap_ext_type = FAMFS_EXT_INTERLEAVE;

	/* We currently only support one interleaved extent - hence index [0] */
	fmap->ie[0].ie_nstrips = lp->interleave_param.nstrips;
	fmap->ie[0].ie_chunk_size = lp->interleave_param.chunk_size;
	strips = fmap->ie[0].ie_strips;

	/* Allocate our strips. If nstrips is <  nbuckets,
	 * we can tolerate some failures */
	for (i = 0; i < lp->interleave_param.nbuckets; i++) {
		int bucket_num = bucket_series_next(bs);
		s64 ofs;
		u64 pos = bucket_num * bucket_size_au * lp->alloc_unit;

		/* Oops: bitmap might not be allocated yet */
		ofs = bitmap_alloc_contiguous(lp->bitmap, lp->nbits,
					      lp->alloc_unit,
					      strip_size_au * lp->alloc_unit,
					      &pos,
					      bucket_size_au * lp->alloc_unit);

		if (ofs > 0) {
			strips[nstrips_allocated].se_devindex = 0;
			strips[nstrips_allocated].se_offset = ofs;
			strips[nstrips_allocated].se_len = strip_size_au * lp->alloc_unit;

			if (verbose)
				printf("%s: strip %lld bucket %d ofs 0x%llx len %lld\n",
				       __func__, i, bucket_num, ofs,
				       strip_size_au * lp->alloc_unit);

			nstrips_allocated++;
			if (nstrips_allocated >= lp->interleave_param.nstrips)
				break; /* Got all our strips */
		}
	}

	if (nstrips_allocated < lp->interleave_param.nstrips) {
		/* Allocation failed; got fewer strips than needed */
		fprintf(stderr,
			"%s: failed %lld strips @%lldb each; got %lld strips\n",
			__func__, lp->interleave_param.nstrips,
			strip_size_au * lp->alloc_unit,
			nstrips_allocated);

		if (verbose > 1) {
			printf("%s: before freeing strips from failed alloc:",
			       __func__);
			mu_print_bitmap(lp->bitmap, lp->nbits);
		}

		for (j = 0; j < i; j++)
			bitmap_free_contiguous(lp->bitmap, lp->nbits,
					       lp->alloc_unit,
					       strips[j].se_offset,
					       strips[j].se_len);
		free(fmap);
		if (verbose > 1) {
			printf("%s: after:", __func__);
			mu_print_bitmap(lp->bitmap, lp->nbits);
		}

		return -ENOMEM;
	}
	/* We only support single-interleaved-extent (but multi-strip) alloc: */
	fmap->fmap_niext = 1;
	*fmap_out = fmap;

	if (bs)
		bucket_series_destroy(bs);

	if (verbose > 1)
		mu_print_bitmap(lp->bitmap, lp->nbits);

	return 0;
}

static inline int
alloc_is_interleaved(struct famfs_locked_log *lp)
{
	if (lp->interleave_param.nbuckets ||
	    lp->interleave_param.nstrips  ||
	    lp->interleave_param.chunk_size)
		return 1;

	return 0;
}

int
famfs_file_alloc(
	struct famfs_locked_log     *lp,
	u64                          size,
	struct famfs_log_fmap      **fmap_out,
	int                          verbose)
{
	if (!lp->bitmap) {
		/* Bitmap is needed and hasn't been built yet */
		lp->bitmap = famfs_build_bitmap(lp->logp, lp->alloc_unit,
						lp->devsize, &lp->nbits,
						NULL, NULL, NULL, NULL, verbose);
		if (!lp->bitmap) {
			fprintf(stderr, "%s: failed to allocate bitmap\n", __func__);
			return -1;
		}
		lp->cur_pos = 0;
	}

	if ((FAMFS_KABI_VERSION <= 42) && alloc_is_interleaved(lp)) {
		fprintf(stderr,
			"%s: interleave specified on "
			"non-interleave-capable kernel\n",
			__func__);
		return -1;
	}
	if (!alloc_is_interleaved(lp))
		return famfs_file_alloc_contiguous(lp, size, fmap_out);

	return famfs_file_strided_alloc(lp, size, fmap_out, verbose);
}

void
mu_bitmap_range_stats(
	u8 *bitmap,
	u64 start,
	u64 end, /* exclusive */
	struct famfs_bitmap_stats *bs)
{
	u64 i;
	int prev_bit_free = 0;
	u64 free_ct = 0;

	assert(bs);
	memset(bs, 0, sizeof(*bs));
	bs->size = end - start;
	for (i = start; i < end; i++) {
		switch (mu_bitmap_test(bitmap, i)) {
		case 1:
		/* Skip bits that are set... */
		if (prev_bit_free) {
			/* previous bit was free - last of a free
			 * fragment */
			bs->smallest_free_section =
				MIN(free_ct, bs->smallest_free_section);
			bs->largest_free_section =
				MAX(free_ct, bs->largest_free_section);
		}
		bs->bits_inuse++;
		prev_bit_free = 0;
		bs->bits_inuse++;
		break;

		case 0:
		if (!prev_bit_free) {
			prev_bit_free = 1;
			bs->fragments_free++;
		}
		free_ct++;
		bs->bits_free++;
		break;

		default:
			assert(0);
		}
	}

	/* if the last bit was free, finish up */
	if (prev_bit_free) {
			/* previous bit was free - last of a free
			 * fragment */
			bs->smallest_free_section =
				MIN(free_ct, bs->smallest_free_section);
			bs->largest_free_section =
				MAX(free_ct, bs->largest_free_section);	
	}
}

