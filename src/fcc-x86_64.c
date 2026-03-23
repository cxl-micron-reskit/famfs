// SPDX-License-Identifier: BSD-3-Clause
/*
 * libfcc - Famfs cache coherency library
 * fcc_x86_64.c - x86-64 implementation of cache flush & invalidate functions
 * 
 * Copyright (c) 2014-2023, Intel Corporation. All rights reserved.
 * Portions derived from Intel PMDK (libpmem2) – BSD-3-Clause licensed.
 * Copyright (c) 2026, Micron Technology. All rights reserved.
 */
#if ! (defined(__x86_64__) || defined(_M_X64))
# error "fcc-x86_64.c is being compiled for a non-x86_64 target"
#endif

#include "libfcc.h"
#include <emmintrin.h>   /* _mm_sfence (SFENCE) and _mm_clflush */
#include <cpuid.h>       /* __get_cpuid_count for feature detection */
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include "famfs_log.h"

static const uintptr_t CACHELINE_SIZE = 64;

/* Define types for internal function pointers */
typedef void (*fcc_func_ptr)(uintptr_t addr);
typedef void (*fence_fn_t)(void);

/* Function pointers for the chosen cache flush instructions and fence */
static fcc_func_ptr flush_cacheline_func = NULL;
static fcc_func_ptr invalidate_cacheline_func = NULL;
static fence_fn_t fence_func = NULL;
static pthread_once_t initialized = PTHREAD_ONCE_INIT;

/* Use CLFLUSH to flush and invalidate a cache line */
static inline void x86_flush_clflush(uintptr_t addr)
{
	_mm_clflush((const void*)addr);
}

/* Use CLFLUSHOPT (optimized flush) to flush and */
/* invalidate a cache line (non-serializing) */
static void __attribute__((target("clflushopt")))
x86_flush_clflushopt(uintptr_t addr)
{
	__builtin_ia32_clflushopt((const void *)addr);
}

/* Use CLWB to write back a cache line without invalidating it */
static inline void x86_flush_clwb(uintptr_t addr)
{
	/* CLWB has opcode 66 0F AE /6 */
	__asm__ volatile(".byte 0x66, 0x0f, 0xae, 0x30" : "+m" (*(volatile char *)addr));
	/* No invalidation: the cache line remains in cache in a clean state. */
}

/* Memory barrier implementations: */
static inline void x86_sfence(void)
{
	/* SFENCE: ensures all previous store operations (including flushes) complete */
	_mm_sfence();
}

/* Initialize function pointers based on CPU features
 * (detect if CLWB/CLFLUSHOPT are available)
 */
static void x86_init_flush_functions(void)
{
	unsigned int eax, ebx, ecx, edx;
	int has_clflushopt = 0;
	int has_clflush = 0;
	int has_clwb = 0;

	/* CPUID leaf 7, sub-leaf 0: EBX bits 23 = CLFLUSHOPT, 24 = CLWB */
	if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
		has_clflushopt = (ebx >> 23) & 1;
		has_clwb       = (ebx >> 24) & 1;
	}
	/* Check for CLFLUSH support using CPUID leaf 1
	 * CLFSH feature is indicated by bit 19 of the EDX register
	 */
	__get_cpuid(1, &eax, &ebx, &ecx, &edx);
	has_clflush = (edx >> 19) & 1;

	/* Select optimal flush instructions */
	if (has_clwb) {
		assert(has_clflush);
		flush_cacheline_func = x86_flush_clwb;
		/* If CLFLUSHOPT is also available, use it for invalidation,
		 * else fall back to CLFLUSH */
		if (has_clflushopt)
			invalidate_cacheline_func = x86_flush_clflushopt;
		else
			invalidate_cacheline_func = x86_flush_clflush;
	} else if (has_clflushopt) {
		flush_cacheline_func = x86_flush_clflushopt;
		invalidate_cacheline_func = x86_flush_clflushopt;
	} else if (has_clflush) {
		/* Only CLFLUSH is available */
		flush_cacheline_func = x86_flush_clflush;
		invalidate_cacheline_func = x86_flush_clflush;
	}
	fence_func = x86_sfence;
}

/* Flush a range of memory [addr, addr+len) using the flush function */
static void x86_flush_range(uintptr_t start, size_t len, fcc_func_ptr fcc_func)
{
	if (len == 0)
		return;

	/* Align to cache line boundary (64 bytes on x86-64) */
	uintptr_t ptr = start & ~(CACHELINE_SIZE - 1);
	uintptr_t end = start + len;
	famfs_log(FAMFS_LOG_DEBUG,
		  "start = 0x%" PRIxPTR " ptr = 0x%" PRIxPTR " end: 0x%" PRIxPTR "\n",
		  (uintptr_t)start, (uintptr_t)ptr, (uintptr_t)end );
	
	for (; ptr < end; ptr += CACHELINE_SIZE) {
		fcc_func(ptr);
	}
}

void flush_processor_cache(const void *addr, size_t len)
{
	pthread_once(&initialized, x86_init_flush_functions);
	famfs_log(FAMFS_LOG_DEBUG,
			"flush_processor_cache 0x%" PRIxPTR " %lu \n",
			(uintptr_t)addr, len);

	/* Ensure all flush instructions have completed and data is visible */
	x86_flush_range((uintptr_t)addr, len, flush_cacheline_func);
	fence_func(); /* ensure all prior memory ops complete before flushing */
}

void invalidate_processor_cache(const void *addr, size_t len)
{
	pthread_once(&initialized, x86_init_flush_functions);
	/* Invalidate: flush and invalidate each line */
	famfs_log(FAMFS_LOG_DEBUG,
		  "invalidate_processor_cache 0x%" PRIxPTR " %lu \n",
		  (uintptr_t)addr, len);
	x86_flush_range((uintptr_t)addr, len, invalidate_cacheline_func);
	fence_func();
}

void hard_flush_processor_cache(const void *addr, size_t len)
{
	pthread_once(&initialized, x86_init_flush_functions);
	/* Use a full memory barrier before and after to enforce strong ordering */
	famfs_log(FAMFS_LOG_DEBUG,
		  "hard_flush_processor_cache 0x%" PRIxPTR " %lu \n",
		  (uintptr_t)addr, len);
	fence_func(); /* ensure all prior memory ops complete before flushing */
	x86_flush_range((uintptr_t)addr, len, invalidate_cacheline_func);
	fence_func(); /* ensure all prior memory ops complete before flushing */
}
