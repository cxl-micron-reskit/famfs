// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2023-2026 Micron Technology, Inc.  All rights reserved.
 */

/* mmap_bench.c
 * Usage: mmap_bench <path> [sizes_csv]
 * - Opens an existing file, mmaps it (MAP_SHARED, PROT_READ|PROT_WRITE).
 * - For each block size in sizes_csv (default "4K,64K,1M"):
 *  1) Sequential WRITE: full-file memcpy + msync(MS_SYNC)
 *  2) Sequential READ: touch 1 byte per 4K page across the full file
 *  3) Random WRITE: run for MMAP_RAND_SECS (default 60s), exact wall time
 *  4) Random READ: same duration, exact wall time
 * - Deterministic behavior options:
 *    MMAP_RAND_SECS: duration seconds (double), default 60
 *    MMAP_SEED: uint64 seed (default: time(NULL) ^ addr), for reproducibility
 *
 * Build: gcc -O2 -Wall -Wextra -o mmap_bench mmap_bench.c
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <stdint.h>

static double elapsed_sec(struct timespec a, struct timespec b)
{
	return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

static int timespec_now(struct timespec *ts)
{
	if (clock_gettime(CLOCK_MONOTONIC, ts) != 0)
		return -1;
	return 0;
}

static size_t parse_size(const char *s)
{
	// Accepts "K", "M", "G"
	char *end = NULL;
	double val = strtod(s, &end);

	if (end == s)
		return 0;
	size_t mult = 1;
	if (*end) {
		char c = toupper((unsigned char)*end);
		if (c == 'K')
			mult = 1024ULL;
		else if (c == 'M')
			mult = 1024ULL * 1024ULL;
		else if (c == 'G')
			mult = 1024ULL * 1024ULL * 1024ULL;
		else
			return 0;
	}
	double bytes_d = val * (double)mult;
	if (bytes_d <= 0.0)
		return 0;
	return (size_t)bytes_d;
}

static void banner_begin(const char *name, size_t bs)
{
	struct timespec t;

	timespec_now(&t);
	printf("%s_BEGIN, bs=%zu, t=%ld.%09ld\n", name, bs, (long)t.tv_sec,
	       t.tv_nsec);
}

static void banner_end(const char *name, size_t bs)
{
	struct timespec t;

	timespec_now(&t);
	printf("%s_END,   bs=%zu, t=%ld.%09ld\n", name, bs, (long)t.tv_sec,
	       t.tv_nsec);
}

static void bench_seq_rw(void *map, size_t filesize, size_t bs)
{
	void *buf = NULL;
	struct timespec s, e;
	volatile unsigned long sink = 0;

	if (bs == 0 || bs > filesize) {
		fprintf(stderr,
			"Skipping block size %zu: invalid for filesize %zu\n",
			bs, filesize);
		return;
	}

	if (posix_memalign(&buf, 4096, bs) != 0) {
		fprintf(stderr, "posix_memalign failed for bs=%zu\n", bs);
		exit(2);
	}
	memset(buf, 0xAB, bs);
	size_t ops = filesize / bs;
	if (ops == 0) {
		fprintf(stderr,
			"Skipping block size %zu: 0 ops for filesize %zu\n", bs,
			filesize);
		free(buf);
		return;
	}

	// WRITE (sequential)
	banner_begin("MMAP_WRITE_SEQ", bs);
	timespec_now(&s);

	for (size_t i = 0; i < ops; i++)
		memcpy((char *)map + i * bs, buf, bs);

	if (msync(map, filesize, MS_SYNC) != 0)
		perror("msync");

	timespec_now(&e);
	double wsecs = elapsed_sec(s, e);
	double wmbps = (filesize / 1024.0 / 1024.0) / wsecs;
	double wiops = ops / wsecs;

	printf("MMAP_WRITE_SEQ, bs=%zu, total_bytes=%zu, elapsed=%.6f sec,"
			" throughput=%.2f MiB/s, iops=%.2f\n", bs, filesize, wsecs, wmbps, wiops);
	banner_end("MMAP_WRITE_SEQ", bs);

	// READ (sequential), touch each 4K within the block
	banner_begin("MMAP_READ_SEQ", bs);
	timespec_now(&s);
	for (size_t i = 0; i < ops; i++) {
		char *p = (char *)map + i * bs;
		for (size_t j = 0; j < bs; j += 4096)
			sink += p[j];
	}
	timespec_now(&e);
	double rsecs = elapsed_sec(s, e);
	double rmbps = (filesize / 1024.0 / 1024.0) / rsecs;
	double riops = ops / rsecs;
	printf("MMAP_READ_SEQ,  bs=%zu, total_bytes=%zu, elapsed=%.6f sec, "
		"throughput=%.2f MiB/s, iops=%.2f\n", bs, filesize, rsecs, rmbps, riops);
	banner_end("MMAP_READ_SEQ", bs);

	(void)sink;
	free(buf);
}

// xorshift64* RNG
static inline uint64_t rng_next(uint64_t *state)
{
	uint64_t x = *state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * 2685821657736338717ULL;
}

// Read MMAP_RAND_SECS (default 60.0)
static double get_rand_secs_default(void)
{
	const char *s = getenv("MMAP_RAND_SECS");

	if (!s || !*s)
		return 60.0;
	double v = atof(s);

	if (v < 0.1)
		v = 0.1;
	if (v > 86400.0)
		v = 86400.0;
	return v;
}

// Read MMAP_SEED (optional deterministic seed)
static uint64_t get_seed_default(uint64_t salt)
{
	const char *s = getenv("MMAP_SEED");
	if (s && *s) {
		char *end = NULL;
		unsigned long long v = strtoull(s, &end, 10);

		if (end != s)
			return (uint64_t)v;
	}
	// fallback: time + provided salt (e.g., pointer)
	return (uint64_t)time(NULL) ^ salt;
}

// Random WRITE for exact duration_sec seconds (checks time every iteration)
static void bench_rand_write(void *map, size_t filesize, size_t bs,
			     double duration_sec)
{
	if (bs == 0 || bs > filesize) {
		fprintf(stderr,
			"Skipping rand write bs=%zu: invalid for filesize %zu\n",
			bs, filesize);
		return;
	}
	size_t align = bs;

	if (align < 4096)
		align = 4096;

	size_t max_index = (filesize - bs) / align;

	if (filesize < bs || max_index == (size_t)-1) {
		fprintf(stderr,
			"Skipping rand write bs=%zu: filesize too small\n", bs);
		return;
	}

	void *buf = NULL;
	if (posix_memalign(&buf, 4096, bs) != 0) {
		fprintf(stderr, "posix_memalign failed for bs=%zu\n", bs);
		exit(2);
	}
	memset(buf, 0xCD, bs);

	uint64_t seed = get_seed_default((uint64_t)(uintptr_t)buf);
	uint64_t ops = 0;
	unsigned long long bytes = 0ULL;

	struct timespec start, now;
	timespec_now(&start);
	banner_begin("MMAP_RAND_WRITE", bs);

	for (;;) {
		// one op per iteration, exact time check each time
		uint64_t r = rng_next(&seed);
		size_t idx = (size_t)(r % (max_index + 1));
		size_t off = idx * align;

		memcpy((char *)map + off, buf, bs);
		ops++;
		bytes += bs;

		timespec_now(&now);
		if (elapsed_sec(start, now) >= duration_sec)
			break;
	}

	// async flush so we don't distort per-op latency
	if (msync(map, filesize, MS_ASYNC) != 0)
		perror("msync(MS_ASYNC)");

	double secs = elapsed_sec(start, now);
	double mbps = (bytes / 1024.0 / 1024.0) / secs;
	double iops = ops / secs;

	printf("MMAP_RAND_WRITE_%us, bs=%zu, ran_seconds=%.3f, ops=%llu, bytes=%llu, "
			"throughput=%.2f MiB/s, iops=%.2f\n", (unsigned int)duration_sec, bs,
			secs, (unsigned long long)ops, bytes, mbps, iops);
	banner_end("MMAP_RAND_WRITE", bs);

	free(buf);
}

// Random READ for exact duration_sec seconds
static void bench_rand_read(void *map, size_t filesize, size_t bs,
			    double duration_sec)
{
	if (bs == 0 || bs > filesize) {
		fprintf(stderr,
			"Skipping rand read bs=%zu: invalid for filesize %zu\n",
			bs, filesize);
		return;
	}
	size_t align = bs;
	if (align < 4096)
		align = 4096;

	size_t max_index = (filesize - bs) / align;
	if (filesize < bs || max_index == (size_t)-1) {
		fprintf(stderr,
			"Skipping rand read bs=%zu: filesize too small\n", bs);
		return;
	}

	uint64_t seed = get_seed_default((uint64_t)(uintptr_t)map);
	uint64_t ops = 0;
	unsigned long long bytes = 0ULL;
	volatile unsigned long sink = 0;

	struct timespec start, now;
	timespec_now(&start);
	banner_begin("MMAP_RAND_READ", bs);

	for (;;) {
		uint64_t r = rng_next(&seed);
		size_t idx = (size_t)(r % (max_index + 1));
		size_t off = idx * align;
		char *p = (char *)map + off;

		for (size_t j = 0; j < bs; j += 4096)
			sink += p[j];

		ops++;
		bytes += bs;

		timespec_now(&now);
		if (elapsed_sec(start, now) >= duration_sec)
			break;
	}

	double secs = elapsed_sec(start, now);
	double mbps = (bytes / 1024.0 / 1024.0) / secs;
	double iops = ops / secs;
	printf("MMAP_RAND_READ_%us,  bs=%zu, ran_seconds=%.3f, ops=%llu, bytes=%llu, "
			"throughput=%.2f MiB/s, iops=%.2f\n", (unsigned int)duration_sec, bs,
			secs, (unsigned long long)ops, bytes, mbps, iops);
	banner_end("MMAP_RAND_READ", bs);

	(void)sink;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <path> [sizes_csv]\n", argv[0]);
		fprintf(stderr,
			"Example: %s /mnt/famfs/mmap_100GB.bin 4K,64K,1M\n",
			argv[0]);
		return -1;
	}

	const char *path = argv[1];
	const char *csv = (argc >= 3) ? argv[2] : "4K,64K,1M";

	// Open existing file
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		perror("open");
		fprintf(stderr, "File must exist and be writable: %s\n", path);
		return -1;
	}

	// Get file size
	struct stat st;
	if (fstat(fd, &st) != 0) {
		perror("fstat");
		close(fd);
		return -1;
	}
	if (!S_ISREG(st.st_mode)) {
		fprintf(stderr, "Path is not a regular file: %s\n", path);
		close(fd);
		return -1;
	}
	size_t filesize = (size_t)st.st_size;
	if (filesize == 0) {
		fprintf(stderr,
			"File size is 0; please create and size the file before running: %s\n",
			path);
		close(fd);
		return -1;
	}

	// Map the file
	void *map =	mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		fprintf(stderr, "mmap failed\n");
		close(fd);
		return -1;
	}

	// POSIX access pattern hint
	int rv = posix_madvise(map, filesize, POSIX_MADV_SEQUENTIAL);
	if (rv != 0) {
		fprintf(stderr, "posix_madvise failed: %d (%s)\n", rv, strerror(rv));
		munmap(map, filesize);
		close(fd);
		return rv;
	}

	// Parse sizes
	char *sizestr = strdup(csv);
	if (!sizestr) {
		perror("strdup");
		munmap(map, filesize);
		close(fd);
		return -1;
	}

	double rand_secs = get_rand_secs_default();
	printf("MMAP_BENCH_BEGIN, file=%s, size_bytes=%zu, sizes_csv=%s, "
			"rand_secs=%.3f\n", path, filesize, csv, rand_secs);

	char *tok = NULL;
	for (tok = strtok(sizestr, ","); tok; tok = strtok(NULL, ",")) {
		while (*tok == ' ' || *tok == '\t')
			tok++;
		size_t bs = parse_size(tok);
		if (bs == 0) {
			fprintf(stderr, "Invalid block size token: '%s'\n",
				tok);
			continue;
		}

		bench_seq_rw(map, filesize, bs);
		bench_rand_write(map, filesize, bs, rand_secs);
		bench_rand_read(map, filesize, bs, rand_secs);
	}

	printf("MMAP_BENCH_END, file=%s\n", path);

	free(sizestr);
	munmap(map, filesize);
	close(fd);
	return 0;
}
