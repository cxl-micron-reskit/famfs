/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2024-2025 Micron Technology, Inc.  All rights reserved.
 */

#ifndef _LINUX_PCQ_H
#define _LINUX_PCQ_H

#define PCQ_MAGIC 0xBEEBEE3
#define PCQ_CONSUMER_MAGIC 0xBEEBEE4

/**
 * struct @pcq
 *
 * @pcq_magic
 * @nbuckets            - number of buckets
 * @bucket_size         - bucket size, inclusive of crc in the last 32 bits
 * @bucket_array_offset - offset within this file of the first bucket
 * @producer_index      - index of the last valid entry; empty if == consumer_index
 * @next_seq            - next seq number (not in same cacche line as producer_index)
 */
struct pcq {
	u64 pcq_magic;
	u64 nbuckets;
	u64 bucket_size;
	u64 bucket_array_offset;
	u64 producer_index;
	char pad[1024];
	u64 next_seq;
	u64 pcq_size;
};

/**
 * struct @pcq_consumer
 *
 * @pcq_consumer_magic
 * @pad
 * @consumer_index - Consumer index for the queue
 * @pad2
 * @next_seq       - Sequence number for next entry from the queue
 */
struct pcq_consumer {
	u32 pcq_consumer_magic;
	u32 pad;
	u64 consumer_index;
	char pad2[1048576];
	u64 next_seq;
	u64 pcqc_size;
};

struct pcq_handle {
	struct pcq *pcq;
	struct pcq_consumer *pcqc;
};

static inline int64_t
pcq_payload_size(struct pcq *pcq)
{
	assert(pcq->pcq_magic == PCQ_MAGIC);
	return pcq->bucket_size - sizeof(unsigned long) - sizeof(u64);
}

static inline u64
pcq_crc_offset(struct pcq *pcq)
{
	return pcq->bucket_size - sizeof(unsigned long);
}

enum pcq_role {
	PRODUCER,
	CONSUMER,
	READONLY,
};

enum stop_mode {
	EMPTY, /* consumer only */
	NMESSAGES,
	STOP_FLAG,
};

struct pcq_thread_arg {
	enum pcq_role role;
	int verbose;
	enum stop_mode stop_mode;
	u64 nmessages;
	u64 runtime;
	u64 seed;
	bool wait;
	char *basename;
	int stop_now;

	/* Outputs */
	u64 nsent;
	u64 nreceived;
	u64 nerrors;
	u64 nfull;  /* # of times full (producer) */
	u64 nempty; /* # of times empty (consumer) */
	u64 retries;
	int result;
};

struct pcq_status_thread_arg {
	struct pcq_thread_arg *p; /* producer */
	struct pcq_thread_arg *c; /* consumer */
	char *basename;
	u64 interval;
	int stop_now;
};

enum pcq_perm {
	pcq_perm_nop=0,
	pcq_perm_none,
	pcq_perm_both,
	pcq_perm_producer,
	pcq_perm_consumer,
};

int pcq_set_perm(const char *filename, enum pcq_perm role);
int pcq_create(char *fname, u64 nbuckets, u64 bucket_size,
	       uid_t uid, gid_t gid, int verbose);
int get_queue_info(const char *fname, FILE *statusfile, int verbose);
int run_producer(struct pcq_thread_arg *a);
void *pcq_worker(void *arg);
void *status_worker(void *arg);
int run_consumer(struct pcq_thread_arg *a);

#endif
