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


static void
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
 * @alloc_range: size (bytes) of range to allocate from (starting at @cur_pos)
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
	u64 alloc_range)
{
	u64 i, j;
	u64 alloc_bits = (alloc_size + FAMFS_ALLOC_UNIT - 1) /  FAMFS_ALLOC_UNIT;
	u64 bitmap_remainder;
	u64 start_idx;
	u64 alloc_range_nbits;

	assert(cur_pos);

	start_idx = *cur_pos / FAMFS_ALLOC_UNIT;
	alloc_range_nbits = (alloc_range) ? (alloc_range / FAMFS_ALLOC_UNIT) : nbits;

	for (i = start_idx; i < nbits; i++) {
		/* Skip bits that are set... */
		if (mu_bitmap_test(bitmap, i))
			continue;

		bitmap_remainder = start_idx + alloc_range_nbits - i;
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

/**
 * famfs_alloc_contiguous()
 *
 * @lp:      locked log struct. Will perform bitmap build if no already done
 * @size:    Size to allocate
 * @verbose:
 */
static s64
famfs_alloc_contiguous(
	struct famfs_locked_log *lp,
	u64 size,
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
	return bitmap_alloc_contiguous(lp->bitmap, lp->nbits, size, &lp->cur_pos, 0);
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

	offset = famfs_alloc_contiguous(lp, size, verbose);
	if (offset < 0) {
		rc = -ENOMEM;
		fprintf(stderr, "%s: Out of space!\n", __func__);
		goto out;
	}

	/* Allocation at offset 0 is always wrong - the superblock lives there */
	assert(offset != 0);

	fmap->fmap_ext_type = FAMFS_EXT_SIMPLE;
	fmap->se[0].se_len = round_size_to_alloc_unit(size);
	fmap->se[0].se_offset = offset;
	fmap->fmap_nextents = 1;

	*fmap_out = fmap;

out:
	return rc;
}

