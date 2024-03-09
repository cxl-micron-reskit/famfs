/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2015-2024 Micron Technology, Inc.  All rights reserved.
 */

#include <string.h>
#include <assert.h>

#include "xrand.h"

void
randomize_buffer(void *buf, size_t len, unsigned int seed)
{
    unsigned int *  tmp = (unsigned int *)buf;
    u_int           last;
    uint64_t        remain = len;
    uint64_t        i;
    struct xrand xr;

    if (len == 0)
        return;

    xrand_init(&xr, seed);
    for (i = 0; remain > 0; i++, remain -= sizeof(*tmp)) {
        if (remain > sizeof(*tmp)) { /* likely */
            tmp[i] = xrand64(&xr);
        } else { /* unlikely */
            last = xrand64(&xr);
            memcpy(&tmp[i], &last, remain);
        }
    }
}

int64_t
validate_random_buffer(void *buf, size_t len, unsigned int seed)
{
    unsigned int *  tmp = (unsigned int *)buf;
    unsigned int    val;
    char *          expect = (char *)&val;
    char *          found;
    uint64_t        remain = len;
    uint64_t        i;
    struct xrand xr;

    if (len == 0)
        return -1; /* success... */

    xrand_init(&xr, seed);
    for (i = 0; remain > 0; i++, remain -= sizeof(*tmp)) {
        val = xrand64(&xr);
        if ((remain >= sizeof(*tmp)) && (val != tmp[i])) { /* Likely */
            return ((int64_t)(len - remain));
        } else if (remain < sizeof(*tmp)) { /* Unlikely */
            found = (char *)&tmp[i];
            if (memcmp(expect, found, remain)) {
                /*
                 * [HSE_REVISIT]
                 * Miscompare offset might be off here
                 */
                return ((int64_t)(len - remain));
            }
        }
    }
    /* -1 is success, because 0..n are valid offsets for an error */
    return -1;
}
