// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Micron Technology, Inc.  All rights reserved.
 */

#ifndef FAMFS_FUSED_H
#define FAMFS_FUSED_H

#include "famfs_fused_icache.h"

enum {
	CACHE_NEVER,
	CACHE_NORMAL,
	CACHE_ALWAYS,
};

struct famfs_ctx {
	int debug;
	int writeback;
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
};

#endif /* FAMFS_FUSED_H */
