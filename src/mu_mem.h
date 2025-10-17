// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2021-2025 Micron Technology, Inc.  All rights reserved.
 */
#ifndef H_MU_MEM
#define H_MU_MEM

#include <stdio.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/param.h>
#include <stdint.h>

extern int mock_flush;

#define CL_SIZE 64
#define CL_SHIFT 6

static inline void
__flush_processor_cache(const void *addr, size_t len)
{
	size_t i;
	const char *buffer = (const char *)addr;

	if (mock_flush)
		return;

	/* Flush the processor cache for the target range */
	for (i=0; i < len; i+=CL_SIZE)
		__builtin_ia32_clflush(&buffer[i]);

}

/**
 * hard_flush_processor_cache()
 * flush/invalidate the cache when we don't know whether the host is
 * writing or reading
 */
static inline void
hard_flush_processor_cache(const void *addr, size_t len)
{
	if (mock_flush)
		return;

	__sync_synchronize();
	__flush_processor_cache(addr, len);
	__sync_synchronize();
}

/**
 * flush_processor_cache()
 * flush data that this host has written to memory
 */
static inline void
flush_processor_cache(const void *addr, size_t len)
{
	if (mock_flush)
		return;

	/* Barier before clflush to guarantee all prior memory mutations
	 * are flushed */
	__sync_synchronize();
	__flush_processor_cache(addr, len);
}

/**
 * invalidate_processor_cache()
 * invalidate the cache so we can see data written from elsewhere
 */
static inline void
invalidate_processor_cache(const void *addr, size_t len)
{
	if (mock_flush)
		return;

	__flush_processor_cache(addr, len);
	__sync_synchronize();
	/* Barrier after the flush to guarantee all subsequent memory accesses
	 * happen after the cache is invalidated
	 */
}

#endif
