
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <linux/limits.h>
#include <errno.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <linux/types.h>
#include <stddef.h>
#include <sys/mman.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */
#include <libgen.h>
#include <assert.h>
#include <sys/param.h> /* MIN()/MAX() */
#include <zlib.h>
#include <sys/file.h>
#include <dirent.h>
#include <linux/famfs_ioctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "famfs_lib.h"
#include "mu_mem.h"
#include "random_buffer.h"
#include "famfs.h"

extern int mock_flush;

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
 * @fname               - Name of primary (producer) file
 *                        (consumer file is "<fname>.consumer")
 * @next_seq            - next seq number (not in same cacche line as producer_index)
 */
struct pcq {
	u64 pcq_magic;
	u64 nbuckets;
	u64 bucket_size;
	u64 bucket_array_offset;
	u64 producer_index;
	char *fname;
	char pad[1024];
	u64 next_seq;
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

bool
pcq_valid(struct pcq_handle *pcqh, int verbose)
{
	if (!pcqh) {
		if (verbose)
			fprintf(stderr, "pcqh null\n");
		return false;
	}
	if (!pcqh->pcq) {
		if (verbose)
			fprintf(stderr, "pcq null\n");
		return false;
	}
	if (!pcqh->pcqc) {
		if (verbose)
			fprintf(stderr, "pcqc null\n");
		return false;
	}
	if (pcqh->pcq->pcq_magic != PCQ_MAGIC) {
		if (verbose)
			fprintf(stderr, "pcq bad magic\n");
		return false;
	}
	if (pcqh->pcqc->pcq_consumer_magic != PCQ_CONSUMER_MAGIC) {
		if (verbose)
			fprintf(stderr, "pcqc bad magic\n");
		return false;
	}
	if (pcqh->pcq->producer_index >= pcqh->pcq->nbuckets) {
		if (verbose)
			fprintf(stderr, "pcq invalid producer_index\n");
		return false;
	}
	if (pcqh->pcqc->consumer_index >= pcqh->pcq->nbuckets) {
		if (verbose)
			fprintf(stderr, "pcq invalid consumer_index\n");
		return false;
	}
	return true;
}

u64
pcq_nmessages(struct pcq_handle *pcqh)
{
	u64 pidx = pcqh->pcq->producer_index;
	u64 cidx = pcqh->pcqc->consumer_index;

	if (pidx == cidx)
		return 0;
	if (pidx < cidx)
		pidx = pidx + pcqh->pcq->nbuckets;
	return pidx - cidx;
}

/**
 * pcq_consumer_fname() - get the consumer file name for a pcq
 *
 * Caller must free the returned string
 */

static char *
pcq_consumer_fname(const char *basename)
{
	char *fname;
	size_t baselen = strlen(basename);

	assert(baselen > 0);
	fname = malloc(strlen(basename) + 10);
	if (!fname)
		return NULL;

	sprintf(fname, "%s.consumer", basename);
	return fname;
}

static inline u64
pcq_seq_offset(struct pcq *pcq)
{
	return pcq_crc_offset(pcq) - sizeof(u64);
}

void *
pcq_alloc_entry(struct pcq_handle *pcqh)
{
	assert(pcqh);
	assert(pcqh->pcq);
	assert(pcqh->pcq->pcq_magic == PCQ_MAGIC);
	return calloc(1, pcqh->pcq->bucket_size);
}

int
pcq_create(
	char *fname,
	u64 nbuckets,
	u64 bucket_size,
	int verbose)
{
	int two_mb = 2 * 1024 * 1024;
	struct pcq_consumer *pcqc;
	char *consumer_fname;
	size_t psz, csz;
	struct pcq *pcq;
	struct stat st;
	int rc, rc2;
	u64 size;
	int fd;

	consumer_fname = pcq_consumer_fname(fname);
	if (verbose)
		printf("%s: creating queue  %s / %s\n", __func__, fname, consumer_fname);

	if (bucket_size & (bucket_size - 1)) {
		fprintf(stderr, "%s: bucket_size %lld must be a power of 2\n",
			__func__, bucket_size);
		free(consumer_fname);
		return -1;
	}

	size = two_mb + (nbuckets * bucket_size);

	/*
	 * Fail if either file already exists
	 */
	rc = stat(consumer_fname, &st);
	rc2 = stat(fname, &st);
	if (rc == 0 || rc2 == 0) {
		fprintf(stderr,
			"%s: can't create pcq %s - something with that name already exists\n",
			__func__, fname);
		return -1;
	}

	/*
	 * Create the consumer file
	 */
	fd = famfs_mkfile(consumer_fname, 0644, 0, 0, two_mb, 1);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to create consumer file\n", __func__);
		return -1;
	}
	close(fd);

	/* Producer maps/creates consumer file first */
	pcqc = famfs_mmap_whole_file(consumer_fname, 0 /* writable */, &csz);
	if (!pcqc) {
		fprintf(stderr, "%s: failed to create consumer file\n", __func__);
		free(consumer_fname);
		return -1;
	}

	pcqc->pcq_consumer_magic = PCQ_CONSUMER_MAGIC;
	pcqc->consumer_index = 0;
	pcqc->next_seq = 0;
	flush_processor_cache(pcqc, sizeof(*pcqc));
	munmap(pcqc, csz); /* We're the producer; will remap read-only */

	/*
	 * Create the producer file
	 */
	fd = famfs_mkfile(fname, 0644, 0, 0, size, 1);
	if (fd < 0) {
		fprintf(stderr, "%s: failed to create producer file\n", __func__);
		return -1;
	}
	close(fd);

	pcq = famfs_mmap_whole_file(fname, 0 /* writable */, &psz);
	if (!pcq)
		return -1;

	pcq->fname = fname;
	pcq->pcq_magic = PCQ_MAGIC;
	pcq->nbuckets = nbuckets;
	pcq->bucket_size = bucket_size;
	pcq->bucket_array_offset = two_mb;
	pcq->producer_index = 0ULL;
	pcq->next_seq = 0;
	flush_processor_cache(pcq, sizeof(*pcq));

	if (verbose) {
		printf("%s: sizeof(crc)=%ld\n", __func__, sizeof(unsigned long));
		printf("%s: bucket_size=%lld\n", __func__, pcq->bucket_size);
		printf("%s: payload_size=%ld\n", __func__, pcq_payload_size(pcq));
	}
	munmap(pcq, psz);
	munmap(pcqc, csz);
	printf("%s: Created queue %s\n", __func__, fname);

	return 0;
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
	u64 seed;
	bool wait;
	char *basename;
	int stop_now;

	/* Outputs */
	u64 nsent;
	u64 nreceived;
	u64 nerrors;
	int result;
};


struct pcq_handle *
pcq_open(
	const char *fname,
	enum pcq_role role,
	int verbose)
{
	int rc;
	size_t psz, csz;
	struct pcq *pcq;
	struct pcq_consumer *pcqc;
	struct pcq_handle *pcqh;
	char *consumer_fname;
	struct stat st;

	consumer_fname = pcq_consumer_fname(fname);

	rc = stat(consumer_fname, &st);
	if (rc) {
		fprintf(stderr, "%s: pcq files not found for queue %s\n", __func__, fname);
		return NULL;
	}

	pcq = famfs_mmap_whole_file(fname, (role == PRODUCER) ? 0:1, &psz);
	if (!pcq)
		return NULL;

	/*
	 * Now remap consumer file read-only
	 */
	pcqc = famfs_mmap_whole_file(consumer_fname, (role == CONSUMER) ? 0:1, &csz);
	if (!pcq) {
		munmap(pcq, psz);
		fprintf(stderr, "%s: failed to create consumer file\n", __func__);
		return NULL;
	}

	pcqh = calloc(1, sizeof(*pcqh));
	pcqh->pcq = pcq;
	pcqh->pcqc = pcqc;

	if (verbose) {
		printf("%s: sizeof(crc)=%ld\n", __func__, sizeof(unsigned long));
		printf("%s: bucket_size=%lld\n", __func__, pcq->bucket_size);
		printf("%s: payload_size=%ld\n", __func__, pcq_payload_size(pcq));
	}

	return pcqh;
}

struct pcq_handle *
pcq_producer_open(const char *fname, int verbose)
{
	return pcq_open(fname, PRODUCER, verbose);
}

struct pcq_handle *
pcq_consumer_open(const char *fname, int verbose)
{
	return pcq_open(fname, CONSUMER, verbose);
}

enum pcq_producer_status {
	PCQ_PUT_GOOD,
	PCQ_PUT_FULL_NOWAIT,
	PCQ_PUT_STOPPED,
};

/**
 * pcq_producer_put() - put an in a pcq
 *
 * NOTE: this function must not be called re-entrantly for the same queue
 */
static enum pcq_producer_status
pcq_producer_put(
	struct pcq_handle *pcqh,
	void  *entry,
	struct pcq_thread_arg *a)
{
	unsigned long crc = crc32(0L, Z_NULL, 0);
	struct pcq_consumer *pcqc = pcqh->pcqc;
	struct pcq *pcq = pcqh->pcq;
	u64 crc_offset, seq_offset;
	unsigned long *crcp;
	void *bucket_addr;
	u64 put_index;
	u64 *seqp;

	/* Bucket size is inclusive of sequence number and crc at the end
	 * Compute pointers to those. We do this in the entry before we memcpy
	 * it into the queue bucket.
	 */
	crc_offset = pcq_crc_offset(pcq);
	seq_offset = pcq_seq_offset(pcq);

	crcp = (unsigned long *)((u64)entry + crc_offset);
	seqp = (u64 *)((u64)entry + seq_offset);

	assert(pcq->pcq_magic == PCQ_MAGIC);
	assert(pcqc->pcq_consumer_magic == PCQ_CONSUMER_MAGIC);

	do {
		put_index = pcq->producer_index;
		invalidate_processor_cache(&pcqc->consumer_index,
					   sizeof(pcqc->consumer_index));
		if (((put_index + 1) % pcq->nbuckets ) != pcqc->consumer_index)
			break; /* Not full - prooceed */
		if (a->stop_now)
			return PCQ_PUT_STOPPED;
		else if (a->wait)
			sched_yield();
		else {
			fprintf(stderr, "%s: queue full no wait\n", __func__);
			return PCQ_PUT_FULL_NOWAIT;
		}
	} while (true);

	/* Set seq and crc in entry before we memcpy it into the bucket */
	*seqp = pcq->next_seq++;
	crc = crc32(crc, entry, pcq_payload_size(pcq) + sizeof(*seqp));
	*crcp = crc;

	if (a->verbose) {
		printf("%s: put_index=%lld seq=%lld\n", __func__, put_index, *seqp);
		printf("%s: bucket_size=%lld seq_offset=%lld crc_offset=%lld crc %lx/%lx\n",
		       __func__, pcq->bucket_size, seq_offset, crc_offset,
		       crc, *crcp);
	}

	/*
	 * Put entry into the queue
	 */
	bucket_addr = (void *)((u64)pcq + pcq->bucket_array_offset +
			       (put_index * pcq->bucket_size));
	memcpy(bucket_addr, entry, pcq->bucket_size);
	flush_processor_cache(bucket_addr, pcq->bucket_size);
	pcq->producer_index = (put_index + 1) % pcq->nbuckets;
	flush_processor_cache(&pcq->producer_index, sizeof(pcq->producer_index));

	a->nsent++;
	return 0;
}

enum pcq_consumer_status {
	PCQ_GET_GOOD,
	PCQ_GET_EMPTY,
	PCQ_GET_STOPPED,
	PCQ_GET_BAD_SEQ,
	PCQ_GET_BAD_CRC,
};

/**
 * pcq_consumer_get() - get an element from a pcq
 *
 * NOTE: this function must not be called re-entrantly for the same queue
 */
static enum pcq_consumer_status
pcq_consumer_get(
	struct pcq_handle *pcqh,
	void  *entry_out,
	u64 *seq_out,
	struct pcq_thread_arg *a)
{
	unsigned long crc = crc32(0L, Z_NULL, 0);
	struct pcq_consumer *pcqc = pcqh->pcqc;
	struct pcq *pcq = pcqh->pcq;
	u64 crc_offset, seq_offset;
	unsigned long *crcp;
	void *bucket_addr;
	u64 seq_expect;
	u64 get_index;
	u64 *seqp;

	assert(pcq->pcq_magic == PCQ_MAGIC);
	assert(pcqc->pcq_consumer_magic == PCQ_CONSUMER_MAGIC);

	do {
		get_index = pcqc->consumer_index;
		invalidate_processor_cache(&pcq->producer_index,
					   sizeof(pcq->producer_index));
		if (get_index != pcq->producer_index)
			break;
		else if (a->stop_now)
			return PCQ_GET_STOPPED;
		else if (a->wait)
			sched_yield();
		else {
			if (a->verbose > 1)
				printf("%s: queue empty\n", __func__);
			return PCQ_GET_EMPTY;
		}
	} while (true);

	/* Get entry from queue */
	bucket_addr = (void *)((u64)pcq + pcq->bucket_array_offset +
			       (get_index * pcq->bucket_size));
	invalidate_processor_cache(bucket_addr, pcq->bucket_size);
	memcpy(entry_out, bucket_addr, pcq->bucket_size);

	/* Update queue metadata */
	seq_expect = pcqc->next_seq++;
	pcqc->consumer_index = (pcqc->consumer_index + 1) % pcq->nbuckets;
	flush_processor_cache(&pcqc->consumer_index, sizeof(pcqc->consumer_index));
	a->nreceived++;

	/* Check crc and seq number */
	crc_offset = pcq_crc_offset(pcq);
	seq_offset = pcq_seq_offset(pcq);
	crcp = (unsigned long *)((u64)entry_out + crc_offset);
	seqp = (u64 *)((u64)entry_out + seq_offset);

	if (a->verbose) {
		printf("%s: bucket=%lld\n", __func__, get_index);
		printf("%s: bucket_size=%lld seq_offset=%lld crc_offset=%lld crc %lx/%lx\n",
		       __func__, pcq->bucket_size, seq_offset, crc_offset,
		       crc, *crcp);
		printf("%s: get_index=%lld seq=%lld\n", __func__, get_index, *seqp);
	}

	if (*seqp != seq_expect) {
		fprintf(stderr, "%s: seq mismatch %lld / %lld\n", __func__, *seqp, seq_expect);
		return PCQ_GET_BAD_SEQ;	/* No need to check crc of sequence number is wrong */
	}

	crc = crc32(crc, entry_out, pcq_payload_size(pcq) + sizeof(*seqp));
	if (crc != *crcp) {
		fprintf(stderr, "%s: crc mismatch\n", __func__);
		return PCQ_GET_BAD_CRC;
	}

	*seq_out = *seqp;
	return PCQ_GET_GOOD;
}

int
run_producer(struct pcq_thread_arg *a)
{
	enum pcq_producer_status rc;
	struct pcq_handle *pcqh;
	struct pcq_entry *entry;

	pcqh = pcq_producer_open(a->basename, a->verbose);

	if (!pcqh)
		return -1;

	entry = pcq_alloc_entry(pcqh);

	if (a->verbose)
		printf("%s: nmessages=%lld\n", __func__, a->nmessages);

	while (true) {
		if (a->seed)
			randomize_buffer(entry, pcq_payload_size(pcqh->pcq), a->seed);
		rc = pcq_producer_put(pcqh, entry, a);
		if (rc == PCQ_PUT_FULL_NOWAIT) {
			a->nerrors++;
			return -1;
		}

		if (rc == PCQ_PUT_STOPPED)
			return 0;

		assert(rc == PCQ_PUT_GOOD);

		if (a->stop_mode == NMESSAGES && a->nsent == a->nmessages)
			return 0;

		if (a->stop_now)
			return 0;
	}
	return 0;
}

int
run_consumer(struct pcq_thread_arg *a)
{
	struct pcq_entry *entry_out;
	struct pcq_handle *pcqh;
	int64_t ofs;
	u64 seqnum;
	enum pcq_consumer_status rc;

	if (a->stop_mode == EMPTY)
		assert(a->wait == 0);

	pcqh = pcq_consumer_open(a->basename, a->verbose);
	if (!pcqh)
		return -1;

	entry_out = pcq_alloc_entry(pcqh);

	if (a->verbose)
		printf("%s: nmessages=%lld\n", __func__, a->nmessages);

	while (true) {
		rc = pcq_consumer_get(pcqh, entry_out, &seqnum, a);
		if (rc == PCQ_GET_EMPTY && a->stop_mode == EMPTY)
			return 0;

		if (rc == PCQ_GET_GOOD && a->seed) {
			ofs = validate_random_buffer(entry_out,
						     pcq_payload_size(pcqh->pcq),
						     a->seed);
			if (ofs != -1) {
				fprintf(stderr, "%s: miscompare seq=%lld ofs=%ld\n",
					__func__, seqnum, ofs);
				a->nerrors++;
				continue;
			}
		}

		if (a->stop_now)
			return 0;
		if (a->stop_mode == NMESSAGES && a->nreceived == a->nmessages)
			return 0;

	}
	return 0;
}

void *
pcq_worker(void *arg)
{
	void *rc = NULL;

	struct pcq_thread_arg *a = arg;

	switch (a->role) {
	case PRODUCER:
		a->result = run_producer(a);
		break;

	case CONSUMER:
		a->result = run_consumer(a);
		break;
	case READONLY:
	}
	return rc;
}

int
get_queue_info(const char *fname, FILE *statusfile, int verbose)
{
	struct pcq_handle *pcqh;
	u64 nmessages;
	int rc = 0;

	pcqh = pcq_open(fname, READONLY, verbose);

	if (!pcqh) {
		rc = -1;
		goto out;
	}

	if (!pcq_valid(pcqh, verbose)) {
		rc = -1;
		goto out;
	}
	nmessages = pcq_nmessages(pcqh);
	printf("%s: queue %s contains %lld messages p next_seq %lld c next_seq %lld\n",
	       __func__, fname, nmessages, pcqh->pcq->next_seq, pcqh->pcqc->next_seq);


out:
	if (statusfile)
		fprintf(statusfile, "%lld", nmessages);
	return rc;
}

/* XXX Move this to famfs_lib_util.c or some such - it is now shared with the cli */
static s64 get_multiplier(const char *endptr)
{
	size_t multiplier = 1;

	if (!endptr)
		return 1;

	switch (*endptr) {
	case 'k':
	case 'K':
		multiplier = 1024;
		break;
	case 'm':
	case 'M':
		multiplier = 1024 * 1024;
		break;
	case 'g':
	case 'G':
		multiplier = 1024 * 1024 * 1024;
		break;
	case 0:
		return 1;
	}
	++endptr;
	if (*endptr) /* If the unit was not the last char in string, it's an error */
		return -1;
	return multiplier;
}

void
pcq_usage(int   argc,
	    char *argv[])
{
	char *progname = argv[0];

	printf("\n"
	       "pcq: Run a producer/consumer queue test\n"
	       "\n"
	       "This test creates a set of files to use as a producer/consumer queue\n"
	       "and sends messages through the queue. You can run one copy of this program\n"
	       "that does both the producer & consumer functions, but to test shared memory\n"
	       "you need to run one copy of this program as producer an a second copy (on a\n"
	       "different node) as consumer.\n"
	       "\n"
	       "A producer/consumer queue is implemented as a set of files. The producer file\n"
	       "contains the producer index and the buckets. The consumer file contains the\n"
	       "consumer index. The producer maps the producer file writable and the consumer\n"
	       "file read-only; the consumer does the opposite.\n"
	       "\n"
	       "EXAMPLES:\n"
	       "Create a producer/consumer queue with 4096 buckets which are 1K each:\n"
	       "    %s --create --bsize 1024 --nbuckets 4K <queuename>\n"
	       "\n"
	       "Just run a producer:\n"
	       "    %s --producer [Args] /mnt/famfs/<queuename>\n"
	       "\n"
	       "Run a consumer:\n"
	       "    %s --consumer [Args] /mnt/famfs/<queuename>\n"
	       "\n"
	       "Run a producer and a consumer from a single process:\n"
	       "    %s --producer --consumer [Args] /mnt/famfs/<queuename>\n"
	       "\n"
	       "Drain a pcq\n"
	       "    %s --drain [Args] /mnt/famfs/<queuename>\n"
	       "\n"
	       "Check the state of a producer/consumer queue (maps both fies read-only:\n"
	       "    %s --info [Args] /mnt/famfs/<queuename>\n"
	       "\n"
	       "Arguments:\n"
	       "\n"
	       "Queue Creation:\n"
	       "    -C|--create               - Create a producer/consumer queue\n"
	       "    -b|--bsize <bucketsize>   - size of messages including sequence number\n"
	       "                                and crc (ignored if queue already exists)\n"
	       "    -n|--nbuckets <nnbuckets> - Number of buckets in the queue\n"
	       "                                (ignored if queue already exists)\n"
	       "\n"
	       "Running producers and consumers:"
	       "    -N|--nmessages <n>        - Number of messages to send and/or receive\n"
	       "    -t|--time <seconds>       - Run for the specified duration\n"
	       "    -S|--seed <seed>          - Use seed to generate payload\n"
	       "    -p|--producer             - Run the producer\n"
	       "    -c|--consumer             - Run the consumer\n"
	       "\n"
	       "Special options:\n"
	       "    -i|--info                 - Dump the state of a queue\n"
	       "    -d|--drain                - Run a consumer to drain a queue to empty and\n"
	       "                                then exit. (Note this probably won't do what\n"
	       "                                you want if a producer is running...)\n"
	       "    -D|--dontflush            - Don't issue processor cache flushes and\n"
	       "                                invalidates\n"
	       "    -f|--statusfile           - Write exit status to file (for testing)\n"
	       "    -?                        - Print this message\n"
	       "\n", progname, progname, progname, progname, progname, progname);
}

int
main(int argc, char **argv)
{
	pthread_t producer_thread, consumer_thread;
	struct pcq_thread_arg prod = { 0 };
	struct pcq_thread_arg cons = { 0 };
	char *statusfname = NULL;
	char *filename = NULL;
	bool producer = false;
	bool consumer = false;
	bool create = false;
	u64 bucket_size = 0;
	bool drain = false;
	u64 nmessages = 0;
	bool info = false;
	FILE *statusfile;
	u64 nbuckets = 0;
	int wait = true;
	int runtime = 0;
	int verbose = 0;
	s64 seed = 0;
	int arg_ct;
	int c, rc;
	s64 mult;

	struct option pcq_options[] = {
		/* These options set a flag. */
		{"bsize",       required_argument,        0,  'b'},
		{"nbuckets",    required_argument,        0,  'n'},
		{"seed",        required_argument,        0,  'S'},
		{"nmessages",   required_argument,        0,  'N'},
		{"statusfile",  required_argument,        0,  'f'},
		{"time",        required_argument,        0,  't'},

		{"create",      no_argument,              0,  'C'},
		{"producer",    no_argument,              0,  'p'},
		{"consumer",    no_argument,              0,  'c'},
		{"info",        no_argument,              0,  'i'},
		{"drain",       no_argument,              0,  'd'},
		{"dontflush",   no_argument,              0,  'D'},
		/* These options don't set a flag.
		 * We distinguish them by their indices.
		 */
		/*{"dryrun",       no_argument,       0, 'n'}, */
		{0, 0, 0, 0}
	};

	/* Note: the "+" at the beginning of the arg string tells getopt_long
	 * to return -1 when it sees something that is not recognized option
	 * (e.g. the command that will mux us off to the command handlers
	 */
	while ((c = getopt_long(argc, argv, "+b:s:S:n:N:f:t:CdpcwDih?v",
				pcq_options, &optind)) != EOF) {
		char *endptr;

		arg_ct++;
		switch (c) {

		case 'i':
			info = true;
			break;

			/* Create operations */
		case 'C':
			create = true;
			break;

		case 'b':
			bucket_size = strtoull(optarg, &endptr, 0);
			mult = get_multiplier(endptr);
			if (mult > 0)
				bucket_size *= mult;
			printf("bucket_size=%lld\n", bucket_size);
			break;

		case 'n':
			nbuckets = strtoull(optarg, &endptr, 0);
			mult = get_multiplier(endptr);
			if (mult > 0)
				nbuckets *= mult;
			printf("nbuckets=%lld\n", nbuckets);
			break;

		case 'S':
			seed = strtoull(optarg, 0, 0);
			break;

		case 't':
			runtime = strtoull(optarg, 0, 0);
			break;

		case 'p':
			producer = true;
			break;

		case 'c':
			consumer = true;
			break;

		case 'd':
			drain = true;
			wait = false;
			break;

		case 'v':
			verbose++;
			break;

		case 'N':
			nmessages = strtoull(optarg, &endptr, 0);
			mult = get_multiplier(endptr);
			if (mult > 0)
				nmessages *= mult;
			break;
			break;

		case 'f':
			/* Write execution status to this file (for testing) */
			statusfname= optarg;
			break;

		case 'D':
			mock_flush = 1;
			break;

		case 'h':
		case '?':
			pcq_usage(argc, argv);
			return 0;
		}
	}

	if (info && (create || producer || consumer || drain)) {
		fprintf(stderr, "%s: info not compatible with operating on a pcq\n\n",
			argv[0]);
		pcq_usage(argc, argv);
		return -1;
	}
	if (create && (bucket_size == 0 || nbuckets == 0)) {
		fprintf(stderr, "%s: create requires a bucketsize an nbuckets\n\n", argv[0]);
		pcq_usage(argc, argv);
		return -1;
	}
	if (drain && (wait || producer || nmessages || runtime)) {
		fprintf(stderr,
			"%s: drain can't be used with producer, time or nmessages options\n\n",
			argv[0]);
		pcq_usage(argc, argv);
		return -1;
	}
	if (runtime && nmessages) {
		fprintf(stderr,
			"%s: the --nmessages and --time args cannot be used together\n\n",
			__func__);
		pcq_usage(argc, argv);
		return -1;
	}
	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify base filename\n\n");
		return -1;
	}
	filename = argv[optind++];

	if (statusfname) {
		truncate(statusfname, 0);
		statusfile = fopen(statusfname, "w+");
		assert(statusfile);
	}

	if (create)
		return pcq_create(filename, nbuckets, bucket_size, verbose);

	if (info)
		return get_queue_info(filename, statusfile, verbose);
	if (drain) {
		struct pcq_thread_arg ta = { 0 };

		ta.role = CONSUMER;
		ta.stop_mode = EMPTY;
		ta.basename = filename;
		ta.verbose = verbose;

		rc = run_consumer(&ta);
		if (ta.nerrors) {
			if (statusfile) {
				fprintf(statusfile, "%lld", -ta.nerrors);
				fclose(statusfile);
			}
			printf("pcq: drained %lld messages from queue %s, with %lld errors\n",
			       ta.nreceived, filename, ta.nerrors);
			return -(int)ta.nerrors;
		}

		if (statusfile) {
			printf("pcq: drained %lld messages from queue %s, with no errors\n",
			       ta.nreceived, filename);
			fprintf(statusfile, "%lld", ta.nreceived);
			fclose(statusfile);
		}
		return 0;
	}

	/*
	 * Start the producer thread if needed
	 */
	assert(wait);
	if (producer) {
		prod.role = PRODUCER;
		prod.stop_mode = (runtime) ? STOP_FLAG : NMESSAGES;
		prod.nmessages = nmessages;
		prod.basename = filename;
		prod.seed = seed;
		prod.wait = wait;
		prod.verbose = verbose;
		rc = pthread_create(&producer_thread, NULL, pcq_worker, (void *)&prod);
		if (rc) {
			fprintf(stderr, "%s: failed to start producer thread\n", __func__);
		}
		sleep(0);
	}

	/*
	 * Start the consumer thread
	 */
	if (consumer) {
		cons.role = CONSUMER;
		cons.stop_mode = (runtime) ? STOP_FLAG : NMESSAGES;
		cons.nmessages = nmessages;
		cons.basename = filename;
		cons.seed = seed;
		cons.wait = wait;
		cons.verbose = verbose;
		rc = pthread_create(&consumer_thread, NULL, pcq_worker, (void *)&cons);
		if (rc) {
			fprintf(stderr, "%s: failed to start consumer thread\n", __func__);
		}
	}

	if (runtime) {
		sleep(runtime);
		prod.stop_now = 1;
		cons.stop_now = 1;
	}

	if (producer) {
		rc = pthread_join(producer_thread, NULL);
		if (rc)
			fprintf(stderr, "%s: failed to join producer thread\n", __func__);
	}
	if (consumer) {
		rc = pthread_join(consumer_thread, NULL);
		if (rc)
			fprintf(stderr, "%s: failed to join consumer thread\n", __func__);
	}

	printf("\nQueue:    %s\n", filename);
	printf("Producer: nsent=%lld nerrors=%lld\n", prod.nsent, prod.nerrors);
	printf("Consumer: nreceived=%lld nerrors=%lld\n", cons.nreceived, cons.nerrors);

	/* XXX
	 * The idea here is return
	 * * the sum of errors (negative) if there are errors
	 * * The sum of messages send and received if there are no errors.
	 * but this may overflow an int. If tests don't set the counts too high,
	 * it will work.
	 */
	if (prod.nerrors || cons.nerrors) {
		if (statusfile) {
			fprintf(statusfile, "%lld", -(prod.nerrors + cons.nerrors));
			fclose(statusfile);
		}
		return -(int)(prod.nerrors + cons.nerrors);
	}

	if (statusfile) {
		fprintf(statusfile, "%lld", (prod.nsent + cons.nreceived));
		fclose(statusfile);
	}
	return 0;
}
