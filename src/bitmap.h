/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2015-2024 Micron Technology, Inc. All rights reserved.
 */
#ifndef _H_MSE_PLATFORM_BITMAP
#define _H_MSE_PLATFORM_BITMAP

#define BYTE_SHIFT 3

static inline int
mu_bitmap_size(int num_blocks)
{
	return((num_blocks + 8 - 1) >> BYTE_SHIFT);
}

#define mu_bitmap_foreach(bitmap, max_blk, index, value)	\
	for (index = 0, value = mu_bitmap_test(bitmap, index);	\
	     index < max_blk;					\
	     index++, value = mu_bitmap_test(bitmap, index))

void make_bit_string(u8 byte, char *str);

#ifndef unlikely
#define unlikely __glibc_unlikely
#endif

/*
 *  Inline routines for 64-bit offsets
 */

/**
 * mu_bitmap_test()
 *
 * Return values:
 * 1 - bit is set
 * 0 - bit is not set
 */
static inline int
mu_bitmap_test(u8 *bitmap, s64 index)
{
	s64 byte_num = index >> BYTE_SHIFT;
	s64 bit_num  = index % 8;

	if (bitmap[byte_num] & (1 << bit_num))
		return 1;

	return 0;
}

/**
 * mu_bitmap_set()
 *
 */
static inline void
mu_bitmap_set(u8 *bitmap, s64 index)
{
	s64 byte_num = index >> BYTE_SHIFT;
	s64 bit_num  = index % 8;

	bitmap[byte_num] |= (u8)(1 << bit_num);
}

/**
 * mu_bitmap_test_and_set()
 *
 * Return values:
 * 1 - Bit was not already set, but it is now
 * 0 - Bit was already set
 */
static inline int
mu_bitmap_test_and_set(u8 *bitmap, s64 index)
{
	s64 byte_num = index >> BYTE_SHIFT;
	s64 bit_num  = index % 8;

	if (unlikely(bitmap[byte_num] & (1 << bit_num)))
		return 0;

	bitmap[byte_num] |= (u8)(1 << bit_num);
	return 1;
}

/**
 * mu_bitmap_test_and_clear()
 *
 * Return values:
 * 1 - Bit was set, and has been cleared
 * 0 - Bit was already clear
 */
static inline int
mu_bitmap_test_and_clear(u8 *bitmap, s64 index)
{
	s64 byte_num = index >> BYTE_SHIFT;
	s64 bit_num  = index % 8;
	u8 and_val;

	if (unlikely(!(bitmap[byte_num] & (1 << bit_num))))
		return 0;

	and_val = 0xff ^ (u8)(1 << bit_num);
	bitmap[byte_num] &= and_val;
	return 1;
}

/*
 * Inline routines for 32-bit offsets
 */

/**
 * mse_bitmap_test32()
 *
 * Return values:
 * 1 - bit is set
 * 0 - bit is not set
 */
static inline int
mse_bitmap_test32(
	u8 *bitmap,
	u32 index)
{
	const u32 byte_num = index >> BYTE_SHIFT;
	const u32 bit_num  = index % 8;

	if (bitmap[byte_num] & (1 << bit_num))
		return 1;

	return 0;
}

/**
 * mse_bitmap_set32()
 *
 */
static inline void
mse_bitmap_set32(
	u8 *bitmap,
	u32 index)
{
	const u32 byte_num = index >> BYTE_SHIFT;
	const u32 bit_num  = index % 8;

	bitmap[byte_num] |= (u8)(1 << bit_num);
}

/**
 * mse_bitmap_test_and_set32()
 *
 * Return values:
 * 1 - Bit was not already set, but it is now
 * 0 - Bit was already set
 */
static inline int
mse_bitmap_test_and_set32(
	u8 *bitmap,
	u32 index)
{
	const u32 byte_num = index >> BYTE_SHIFT;
	const u32 bit_num  = index % 8;

	if (unlikely(bitmap[byte_num] & (1 << bit_num)))
		return 0;

	bitmap[byte_num] |= (u8)(1 << bit_num);
	return 1;
}

/**
 * mse_bitmap_test_and_clear32()
 *
 * Return values:
 * 1 - Bit was set, and has been cleared
 * 0 - Bit was already clear
 */
static inline int
mse_bitmap_test_and_clear32(
	u8 *bitmap,
	u32 index)
{
	const u32 byte_num = index >> BYTE_SHIFT;
	const u32 bit_num  = index % 8;
	u8        and_val;

	if (unlikely(!(bitmap[byte_num] & (1 << bit_num))))
		return 0;

	and_val = 0xff ^ (u8)(1 << bit_num);
	bitmap[byte_num] &= and_val;
	return 1;
}

#endif
