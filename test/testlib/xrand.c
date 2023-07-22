/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2020-2021 Micron Technology, Inc.  All rights reserved.
 */

//#include <hse_util/arch.h>
#include <unistd.h>
#include <time.h>
#include "xrand.h"

struct xrand xrand_tls;

static uint64_t
get_cycles(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void
xrand_init(struct xrand *xr, u_int64_t seed)
{
    if (!seed) {
        while (1) {
            seed = (seed << 16) | ((get_cycles() >> 1) & 0xffffu);
            if (seed >> 48)
                break;

            usleep(seed % 127); /* leverage scheduling entropy */
        }
    }

    xoroshiro128plus_init(xr->xr_state, seed);
}

u_int64_t
xrand_range64(struct xrand *xr, u_int64_t lo, u_int64_t hi)
{
    /* compute rv: 0 <= rv < 1  */
    double rand_max = (double)((u_int64_t)-1);
    double rv = (double)xrand64(xr) / (rand_max + 1.0);

    /* scale rv to the desired range */
    return (u_int64_t)((double)lo + (double)(hi - lo) * rv);
}
