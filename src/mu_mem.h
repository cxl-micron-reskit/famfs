// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2021-2024 Micron Technology, Inc.  All rights reserved.
 */
#ifndef H_MU_MEM
#define H_MU_MEM

#include <stdio.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/param.h>

#define CL_SIZE 64
#define CL_SHIFT 6

static inline void
flush_processor_cache(const void *addr, size_t len)
{
	int i;
	const char *buffer = (const char *)addr;

	__sync_synchronize(); /* Full barrier */

	/* Flush the processor cache for the target range */
	for (i=0; i<len; i+=CL_SIZE) {
		__builtin_ia32_clflush(&buffer[i]);
	}

	__sync_synchronize(); /* Full barrier */
}

#endif
