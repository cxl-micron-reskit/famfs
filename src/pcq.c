/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2024-2025 Micron Technology, Inc.  All rights reserved.
 */

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
#include "pcq.h"

extern int mock_flush;

void
pcq_usage(int argc,
	    char *argv[])
{
	char *progname = argv[0];
	(void)argc;

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
	       "Queue permissions:\n"
	       "    -P|--setperm <p|c|b|n>    - Set permissions on a queue for (p)roducer or\n"
	       "                                (c)onsumer, (b)oth or (n)either on this node.\n"
	       "                                Must run separately from create|producer|consumer|drain\n"
	       "\n"
	       "Running producers and consumers:\n"
	       "    -N|--nmessages <n>        - Number of messages to send and/or receive\n"
	       "    -t|--time <seconds>       - Run for the specified duration\n"
	       "    -S|--seed <seed>          - Use seed to generate payload\n"
	       "    -p|--producer             - Run the producer\n"
	       "    -c|--consumer             - Run the consumer\n"
	       "    -s|--status <interval>    - Print status at the specified interval\n"
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
	pthread_t producer_thread, consumer_thread, status_thread;
	struct pcq_status_thread_arg status = { 0 };
	struct pcq_thread_arg prod = { 0 };
	struct pcq_thread_arg cons = { 0 };
	enum pcq_perm role = pcq_perm_nop;
	char *statusfname = NULL;
	FILE *statusfile = NULL;
	u64 status_interval = 0;
	char *filename = NULL;
	bool producer = false;
	bool consumer = false;
	bool create = false;
	u64 bucket_size = 0;
	bool drain = false;
	u64 nmessages = 0;
	bool info = false;
	u64 nbuckets = 0;
	int wait = true;
	int runtime = 0;
	int verbose = 0;
	uid_t uid = 0;
	gid_t gid = 0;
	s64 seed = 0;
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
		{"status",      required_argument,        0,  's'},
		{"setperm",     required_argument,        0,  'P'},
		{"uid",         required_argument,        0,  'u'},
		{"gid",         required_argument,        0,  'g'},

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
	while ((c = getopt_long(argc, argv, "+b:s:S:n:N:f:t:s:u:g:CdpcwDiPh?v",
				pcq_options, &optind)) != EOF) {
		char *endptr;

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

		case 's':
			status_interval = strtoull(optarg, 0, 0);
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

		case 'u':
			uid = strtol(optarg, 0, 0);
			break;

		case 'g':
			gid = strtol(optarg, 0, 0);
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

		case 'f':
			/* Write execution status to this file (for testing) */
			statusfname= optarg;
			break;

		case 'P':
			if (strcmp(optarg, "p") == 0)
				role = pcq_perm_producer;
			else if (strcmp(optarg, "c") == 0)
				role = pcq_perm_consumer;
			else if (strcmp(optarg, "b") == 0)
				role = pcq_perm_both;
			else if (strcmp(optarg, "n") == 0)
				role = pcq_perm_none;
			else {
				fprintf(stderr, "%s: invalid --setperm arg (%s)\n",
					__func__, optarg);
				pcq_usage(argc, argv);
				return 1;
			}
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
		fprintf(stderr,
			"%s: info not compatible with operating on a pcq\n\n",
			argv[0]);
		pcq_usage(argc, argv);
		return 1;
	}
	if (create && (bucket_size == 0 || nbuckets == 0)) {
		fprintf(stderr,
			"%s: create requires bsize and nbuckets\n\n", argv[0]);
		pcq_usage(argc, argv);
		return 1;
	}
	if (!create && (uid || gid)) {
		fprintf(stderr, "%s: uid/gid only apply with --create\n",
			__func__);
		pcq_usage(argc, argv);
		return 1;
	}
	if (create) {
		if (!uid)
			uid = geteuid();
		if (!gid)
			gid = getegid();
	}
	if (drain && (wait || producer || nmessages || runtime)) {
		fprintf(stderr,
			"%s: drain can't be used with producer, time or nmessages options\n\n",
			argv[0]);
		pcq_usage(argc, argv);
		return 1;
	}
	if (runtime && nmessages) {
		fprintf(stderr,
			"%s: the --nmessages and --time args cannot be used together\n\n",
			__func__);
		pcq_usage(argc, argv);
		return 1;
	}
	if (optind > (argc - 1)) {
		fprintf(stderr, "Must specify base filename\n\n");
		return 1;
	}
	filename = argv[optind++];

	if (statusfname) {
		truncate(statusfname, 0);
		statusfile = fopen(statusfname, "w+");
		assert(statusfile);
	}
	if (role != pcq_perm_nop && (create || producer || consumer || drain)) {
		fprintf(stderr,
			"--setperm is incompatible with --create|--drain|--producer|--consumer");
		pcq_usage(argc, argv);
		return 1;
	}

	if (role != pcq_perm_nop)
		return exit_val(pcq_set_perm(filename, role));

	if (create)
		return exit_val(pcq_create(filename, nbuckets, bucket_size,
					   uid, gid,  verbose));

	if (info)
		return exit_val(get_queue_info(filename, statusfile, verbose));
	if (drain) {
		struct pcq_thread_arg ta = { 0 };

		ta.role = CONSUMER;
		ta.stop_mode = EMPTY;
		ta.basename = filename;
		ta.verbose = verbose;

		printf("pcq:    %s\n", filename);
		rc = run_consumer(&ta);
		printf("pcq drain: nreceived=%lld nerrors=%lld nempty=%lld retries=%lld\n",
		       ta.nreceived, ta.nerrors, ta.nempty, ta.retries);
		if (ta.nerrors) {
			if (statusfile) {
				fprintf(statusfile, "%lld", -ta.nerrors);
				fclose(statusfile);
			}
			return exit_val((int)ta.nerrors);
		}

		if (statusfile) {
			printf("pcq: drained %lld messages from queue %s, with no errors\n",
			       ta.nreceived, filename);
			fprintf(statusfile, "%lld", ta.nreceived);
			fclose(statusfile);
		}
		return exit_val(rc);
	}

	/*
	 * Start the producer thread if needed
	 */
	assert(wait);
	if (producer) {
		prod.role = PRODUCER;
		prod.stop_mode = (runtime) ? STOP_FLAG : NMESSAGES;
		prod.nmessages = nmessages;
		prod.runtime = runtime;
		prod.basename = filename;
		prod.seed = seed;
		prod.wait = wait;
		prod.verbose = verbose;
		rc = pthread_create(&producer_thread, NULL, pcq_worker, (void *)&prod);
		if (rc) {
			fprintf(stderr, "%s: failed to start producer thread\n", __func__);
		}
	}

	/*
	 * Start the consumer thread
	 */
	if (consumer) {
		cons.role = CONSUMER;
		cons.stop_mode = (runtime) ? STOP_FLAG : NMESSAGES;
		cons.nmessages = nmessages;
		prod.runtime = runtime;
		cons.basename = filename;
		cons.seed = seed;
		cons.wait = wait;
		cons.verbose = verbose;
		rc = pthread_create(&consumer_thread, NULL, pcq_worker, (void *)&cons);
		if (rc) {
			fprintf(stderr, "%s: failed to start consumer thread\n", __func__);
		}
	}

	if (status_interval) {
		status.p = &prod;
		status.c = &cons;
		status.basename = filename;
		status.interval = status_interval;
		status.stop_now = 0;
		
		rc = pthread_create(&status_thread, NULL, status_worker, (void *)&status);
		if (rc) {
			fprintf(stderr, "%s: failed to start consumer thread\n", __func__);
		}
	}

	if (runtime) {
		sleep(runtime);
		prod.stop_now = 1;
		cons.stop_now = 1;
		status.stop_now = 1;
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
	if (status_interval) {
		status.stop_now = 1;
		rc = pthread_join(status_thread, NULL);
		if (rc)
			fprintf(stderr, "%s: failed to join consumer thread\n", __func__);
	}

	printf("pcq:    %s\n", filename);
	printf("pcq producer: nsent=%lld nerrors=%lld nfull=%lld\n",
	       prod.nsent, prod.nerrors, prod.nfull);
	printf("pcq consumer: nreceived=%lld nerrors=%lld nempty=%lld retries=%lld\n",
	       cons.nreceived, cons.nerrors, cons.nempty, cons.retries);

	if (prod.nerrors || cons.nerrors) {
		if (statusfile) {
			/* If there are errors, the statusfile will contain the negative
			 * sum of the producer and consumer errors
			 */
			fprintf(statusfile, "%lld", -(prod.nerrors + cons.nerrors));
			fclose(statusfile);
		}
		return exit_val((int)(prod.nerrors + cons.nerrors));
	}

	if (statusfile) {
		/* If there are no errors, the statusfile will contain the sum
		 * of messages sent and received
		 */
		fprintf(statusfile, "%lld", (prod.nsent + cons.nreceived));
		fclose(statusfile);
	}
	return exit_val(prod.result + cons.result);
}
