// SPDX-License-Identifier: BSD-3-Clause
//
// libfcc.h - Public API for cache famfs cache coherency control functions.
// 
// Copyright (c) 2014-2023, Intel Corporation. All rights reserved.
// Portions derived from Intel PMDK (libpmem2) – BSD-3-Clause licensed.
// Copyright (c) 2026, <Micron Technology>. All rights reserved.
//
// This header declares functions for explicitly managing CPU cache coherence 
// in multi-host or persistent memory scenarios. The library provides mechanisms 
// to flush and invalidate cache lines on various architectures (x86_64, ARM64, 
// RISC-V, PowerPC), with runtime detection of optimal instructions.

#ifndef LIBFCC_H
#define LIBFCC_H

#include <stddef.h>  // for size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * flush_processor_cache() - Write back modified cache lines to main memory.
 * @addr: Starting address of the memory range.
 * @len: Length of the range in bytes.
 *
 * Ensures that any data in the CPU caches covering the range [addr, addr+len) 
 * is written out to main memory. This function is typically used after
 * updating shared memory, so that other hosts or devices can see the new data.
 */
void flush_processor_cache(const void *addr, size_t len);

/**
 * invalidate_processor_cache() - Invalidate cache lines so future accesses
 * fetch fresh data.
 * @addr: Starting address of the memory range.
 * @len: Length of the range in bytes.
 *
 * Ensures that any cache lines covering the range [addr, addr+len) are
 * invalidated (and written back first if they were dirty) on the local CPU.
 * After this call, subsequent reads from this range will be forced to
 * fetch from main memory. Used before reading memory that may have been
 * modified by another host or program.
 */
void invalidate_processor_cache(const void *addr, size_t len);

/**
 * hard_flush_processor_cache() - hard flush and invalidate of cache lines.
 * @addr: Starting address of the memory range.
 * @len: Length of the range in bytes.
 *
 * Performs a strict flush of the specified range: it writes back any
 * modified data to memory and invalidates the cache lines, surrounding
 * the operation with full memory barriers. This is used in scenarios
 * where strict ordering (both before and after the flush) is required.
 * After this call, the data will be in main memory and not present
 * in the local cache.
 */
void hard_flush_processor_cache(const void *addr, size_t len);

#ifdef __cplusplus
}
#endif

#endif // LIBFCC_H
