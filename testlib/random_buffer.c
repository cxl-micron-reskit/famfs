/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2015-2025 Micron Technology, Inc.  All rights reserved.
 */

#include <string.h>
#include <assert.h>

#include "xrand.h"

void
randomize_buffer(void *buf, size_t len, unsigned int seed)
{
	uint8_t *p = buf;
	uint64_t remain = len;
	uint32_t val;
	struct xrand xr;

	if (len == 0)
		return;

	xrand_init(&xr, seed);

	while (remain >= sizeof(val)) {
		val = xrand64(&xr);
		memcpy(p, &val, sizeof(val));
		p += sizeof(val);
		remain -= sizeof(val);
	}

	if (remain > 0) {
		val = xrand64(&xr);
		memcpy(p, &val, remain);
	}
}

int64_t
validate_random_buffer(void *buf, size_t len, unsigned int seed)
{
	uint8_t *p = buf;
	uint64_t remain = len;
	uint32_t val;
	struct xrand xr;
	int64_t offset = 0;

	if (len == 0)
		return -1;

	xrand_init(&xr, seed);

	while (remain >= sizeof(val)) {
		val = xrand64(&xr);
		if (memcmp(p, &val, sizeof(val)) != 0)
			return offset;
		p += sizeof(val);
		remain -= sizeof(val);
		offset += sizeof(val);
	}

	if (remain > 0) {
		val = xrand64(&xr);
		if (memcmp(p, &val, remain) != 0)
			return offset;
	}

	return -1;
}
