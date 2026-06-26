// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

#ifndef FAMFS_FUSED_H
#define FAMFS_FUSED_H

#include <assert.h>
#include <pthread.h>
#include "famfs_fused_icache.h"

enum {
	CACHE_NEVER,
	CACHE_NORMAL,
	CACHE_ALWAYS,
};

struct famfs_ctx {
	int debug;
	int flock;
	int xattr;
	char *source;
	char *daxdev;
	int max_daxdevs;
	struct famfs_daxdev *daxdev_table;
	double timeout;
	int cache;
	int timeout_set;
	int pass_yaml; /* pass the shadow yaml through */
	int readdirplus;
	struct famfs_icache icache;

	/*
	 * Push-mode daxdev registration (one consumer of the daxdev table).
	 * Daxdevs are pushed to the kernel densely in index order, so the
	 * pushed state is just the highest index pushed so far (-1 = none) -
	 * the fuse analog of the kernel's GET_MAX_DAXDEV. daxdev_push_mutex
	 * serializes the check-and-push against concurrent lookups.
	 */
	pthread_mutex_t daxdev_push_mutex;
	int             daxdev_max_pushed;
};

#endif /* FAMFS_FUSED_H */
