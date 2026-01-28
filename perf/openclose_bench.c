// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */

// openclose_bench.c
// Usage: openclose_bench <dir> <prefix> <count>
// Opens and closes <count> files named <prefix>_N inside <dir>, reports timing.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

static double elapsed_sec(struct timespec a, struct timespec b)
{
	return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "Usage: %s <dir> <prefix> <count>\n", argv[0]);
		return 1;
	}
	const char *dir = argv[1];
	const char *prefix = argv[2];
	int count = atoi(argv[3]);
	char path[4096];
	struct timespec s, e;

	clock_gettime(CLOCK_MONOTONIC, &s);
	for (int i = 1; i <= count; i++) {
		snprintf(path, sizeof(path), "%s/%s_%d", dir, prefix, i);
		int fd = open(path, O_RDONLY);
		if (fd < 0) {
			fd = open(path, O_RDWR);
			if (fd < 0) {
				fprintf(stderr, "open(%s) failed: %s\n", path,
					strerror(errno));
				return 2;
			}
		}
		if (close(fd) != 0) {
			fprintf(stderr, "close(%s) failed: %s\n", path,
				strerror(errno));
			return 3;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &e);
	double secs = elapsed_sec(s, e);
	printf("OPENCLOSE, prefix=%s, count=%d, elapsed=%.6f sec, avg_per_op=%.6f ms\n",
	       prefix, count, secs, (secs * 1000.0) / count);
	return 0;
}
