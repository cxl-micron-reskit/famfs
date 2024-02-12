/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2015-2024 Micron Technology, Inc.  All rights reserved.
 */

#ifndef HSE_CORE_HSE_TEST_RANDOM_BUFFER_H
#define HSE_CORE_HSE_TEST_RANDOM_BUFFER_H

/* randomize_buffer
 *
 * Write pseudo-random data to a buffer, based on a specified seed
 */
void
randomize_buffer(void *buf, size_t len, unsigned int seed);

/*
 * validate_random_buffer
 *
 * Take advantage of the fact that starting with the same seed will generate
 * the same pseudo-random data, for an easy way to validate a buffer
 */
int
validate_random_buffer(void *buf, size_t len, unsigned int seed);

/* generate_random_u_int32_t
 *
 * Create and return a random u_int32_t between min and max inclusive with
 * a uniform distribution.
 */
u_int32_t
generate_random_u_int32_t(u_int32_t min, u_int32_t max);

/* permute_u_int32_t_sequence
 *
 * Given an array of u_int32_t values, randomly permute its elements without
 * introducing repeats.
 */
void
permute_u_int32_t_sequence(u_int32_t *values, u_int32_t num_values);

/* generate_random_u_int32_t_sequence
 *
 * Fill out an array of uniformly distributed random u_int32_t values between min
 * and max inclusive.
 */
void
generate_random_u_int32_t_sequence(u_int32_t min_value, u_int32_t max_value, u_int32_t *values, u_int32_t num_values);

/* generate_random_u_int32_t_sequence_unique
 *
 * Same as generate_random_u_int32_t_sequence(), but all values are unique.
 */
void
generate_random_u_int32_t_sequence_unique(u_int32_t min_value, u_int32_t max_value, u_int32_t *values, u_int32_t num_values);

#endif
