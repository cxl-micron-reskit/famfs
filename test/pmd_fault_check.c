// SPDX-License-Identifier: Apache-2.0
/*
 * pmd_fault_check - verify how an mmap of a DAX (famfs) file is populated.
 *
 * famfs files whose extents are 2 MiB aligned on the dax device, and whose
 * interleave chunk size is a multiple of 2 MiB, should be populated by PMD
 * (2 MiB) faults only. If the fuse server pushes an extent map the kernel
 * cannot map with PMDs (a strip base or chunk size that is a 4 KiB multiple
 * but not a 2 MiB multiple), fs/dax.c silently falls back to PTE (4 KiB)
 * faults: data is still correct, but there are 512x more faults. Nothing in
 * the normal I/O path reports that, so this program measures it.
 *
 * Method: mmap the file MAP_SHARED, touch one byte in every 4 KiB page, and
 * count the page faults the process took (getrusage ru_minflt). DAX mappings
 * have no fault-around, so a PTE-mapped region faults exactly once per 4 KiB
 * page and a PMD-mapped region exactly once per 2 MiB. The expected count
 * for each outcome is predicted from the same rules fs/dax.c applies
 * (dax_fault_check_fallback): a PMD needs matching 2 MiB "colour" between
 * the virtual address and the file offset, must lie entirely within the
 * mapping, and must not extend past EOF. The two predictions differ by
 * about 512x, so a small slack for the process's own stack/heap faults does
 * not blur the verdict.
 *
 * Usage: pmd_fault_check [options] <file>
 *   -w, --write        touch with stores (opens O_RDWR; modifies the file)
 *   -o, --offset OFF   file offset to map from (page aligned; default 0)
 *   -l, --length LEN   bytes to map (default: file size - offset)
 *   -e, --expect MODE  pmd (default) or pte: the verdict required to pass
 *   -m, --misalign     place the mapping at a 2 MiB colour that does NOT
 *                      match the file offset, which forces PTE faults for
 *                      every page: a negative control for the test itself
 *   -s, --slack N      extra faults tolerated (default 16)
 *   -v, --verbose      per-region detail
 *   -h, --help
 *
 * OFF and LEN accept decimal, 0x hex, and a k/m/g suffix.
 *
 * Exit status: 0 = verdict matches --expect, 1 = it does not, 2 = usage or
 * system error (including a mapping that cannot take PMD faults at all when
 * --expect pmd was requested, since that would verify nothing).
 *
 * Run with enough privilege to open the famfs file (usually root/sudo).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#define PMD_SIZE (2UL * 1024 * 1024)
#define PMD_MASK (PMD_SIZE - 1)

static unsigned long page_size;
static int verbose;

struct prediction {
	unsigned long pages;     /* pages touched */
	unsigned long pmds;      /* 2 MiB regions that can take a PMD fault */
	unsigned long pte_pages; /* pages that can only take a PTE fault */
};

static void usage(FILE *fp)
{
	fprintf(fp,
		"Usage: pmd_fault_check [options] <file>\n"
		"  -w, --write        touch with stores instead of loads (modifies the file)\n"
		"  -o, --offset OFF   file offset to map from (page aligned; default 0)\n"
		"  -l, --length LEN   bytes to map (default: file size - offset)\n"
		"  -e, --expect MODE  pmd (default) or pte\n"
		"  -m, --misalign     mismatch the 2 MiB colour on purpose (forces PTE faults)\n"
		"  -s, --slack N      extra faults tolerated (default 16)\n"
		"  -v, --verbose\n"
		"  -h, --help\n");
}

static uint64_t parse_size(const char *s)
{
	char *end;
	uint64_t v;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno || end == s) {
		fprintf(stderr, "pmd_fault_check: bad size '%s'\n", s);
		exit(2);
	}
	switch (*end) {
	case 'k': case 'K': v <<= 10; end++; break;
	case 'm': case 'M': v <<= 20; end++; break;
	case 'g': case 'G': v <<= 30; end++; break;
	default: break;
	}
	if (*end) {
		fprintf(stderr, "pmd_fault_check: bad size '%s'\n", s);
		exit(2);
	}
	return v;
}

static long fault_count(void)
{
	struct rusage ru;

	if (getrusage(RUSAGE_SELF, &ru)) {
		perror("getrusage");
		exit(2);
	}
	return ru.ru_minflt + ru.ru_majflt;
}

/*
 * Predict the fault pattern page by page, mirroring the conditions under
 * which fs/dax.c installs a PMD rather than falling back to PTEs.
 */
static void predict(uintptr_t va, uint64_t off, uint64_t fsize,
		    unsigned long pages, struct prediction *p)
{
	uint64_t max_pgoff = (fsize + page_size - 1) / page_size;
	unsigned long i;

	memset(p, 0, sizeof(*p));
	p->pages = pages;

	for (i = 0; i < pages; i++) {
		uint64_t fo = off + (uint64_t)i * page_size;
		uintptr_t a = va + i * page_size;
		uint64_t pmd_start = fo & ~(uint64_t)PMD_MASK;
		bool colour_ok = (a & PMD_MASK) == (fo & PMD_MASK);
		bool in_map = pmd_start >= off &&
			pmd_start + PMD_SIZE <= off + (uint64_t)pages * page_size;
		bool in_file = ((fo | PMD_MASK) / page_size) < max_pgoff;

		if (colour_ok && in_map && in_file) {
			/* in_map guarantees the first page seen is the PMD start */
			if ((fo & PMD_MASK) == 0)
				p->pmds++;
		} else {
			p->pte_pages++;
		}
	}
}

static unsigned long touch(unsigned char *base, unsigned long pages, bool write)
{
	unsigned long sum = 0;
	unsigned long i;

	for (i = 0; i < pages; i++) {
		volatile unsigned char *p = base + i * page_size;

		if (write)
			*p = (unsigned char)i;
		else
			sum += *p;
	}
	return sum;
}

int main(int argc, char **argv)
{
	static const struct option longopts[] = {
		{ "write",    no_argument,       0, 'w' },
		{ "offset",   required_argument, 0, 'o' },
		{ "length",   required_argument, 0, 'l' },
		{ "expect",   required_argument, 0, 'e' },
		{ "misalign", no_argument,       0, 'm' },
		{ "slack",    required_argument, 0, 's' },
		{ "verbose",  no_argument,       0, 'v' },
		{ "help",     no_argument,       0, 'h' },
		{ 0, 0, 0, 0 },
	};
	const char *expect = "pmd";
	const char *path;
	bool write = false, misalign = false;
	uint64_t off = 0, len = 0, fsize;
	long slack = 16;
	unsigned long pages, expect_pmd, expect_pte;
	long before, after, delta;
	struct prediction pred;
	struct stat st;
	unsigned char *base;
	void *hint = NULL;
	int mflags = MAP_SHARED;
	int fd, c;
	const char *verdict, *result;
	int rc;

	page_size = (unsigned long)sysconf(_SC_PAGESIZE);

	while ((c = getopt_long(argc, argv, "wo:l:e:ms:vh", longopts, NULL)) != -1) {
		switch (c) {
		case 'w': write = true; break;
		case 'o': off = parse_size(optarg); break;
		case 'l': len = parse_size(optarg); break;
		case 'e': expect = optarg; break;
		case 'm': misalign = true; break;
		case 's': slack = strtol(optarg, NULL, 0); break;
		case 'v': verbose++; break;
		case 'h': usage(stdout); return 0;
		default: usage(stderr); return 2;
		}
	}
	if (optind != argc - 1) {
		usage(stderr);
		return 2;
	}
	path = argv[optind];

	if (strcmp(expect, "pmd") && strcmp(expect, "pte")) {
		fprintf(stderr, "pmd_fault_check: --expect must be pmd or pte\n");
		return 2;
	}
	if (off & (page_size - 1)) {
		fprintf(stderr, "pmd_fault_check: offset must be page aligned\n");
		return 2;
	}

	fd = open(path, write ? O_RDWR : O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "pmd_fault_check: open(%s): %s\n", path, strerror(errno));
		return 2;
	}
	if (fstat(fd, &st)) {
		perror("fstat");
		return 2;
	}
	fsize = (uint64_t)st.st_size;
	if (off >= fsize) {
		fprintf(stderr, "pmd_fault_check: offset %" PRIu64 " is at or past EOF (%" PRIu64 ")\n",
			off, fsize);
		return 2;
	}
	if (!len)
		len = fsize - off;
	if (off + len > fsize) {
		fprintf(stderr, "pmd_fault_check: mapping extends past EOF\n");
		return 2;
	}
	/* map whole pages; touching the tail of a partial last page is fine */
	pages = (unsigned long)((len + page_size - 1) / page_size);
	len = (uint64_t)pages * page_size;

	if (misalign) {
		/*
		 * Reserve len + 2 MiB, then place the file mapping one page
		 * past a 2 MiB boundary inside it. With a 2 MiB-aligned file
		 * offset the VA colour then never matches, so every PMD fault
		 * must fall back to PTEs.
		 */
		void *res = mmap(NULL, len + PMD_SIZE, PROT_NONE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (res == MAP_FAILED) {
			perror("mmap (reserve)");
			return 2;
		}
		hint = (void *)((((uintptr_t)res + PMD_MASK) & ~(uintptr_t)PMD_MASK) + page_size);
		mflags |= MAP_FIXED;
	}

	base = mmap(hint, len, write ? PROT_READ | PROT_WRITE : PROT_READ,
		    mflags, fd, (off_t)off);
	if (base == MAP_FAILED) {
		fprintf(stderr, "pmd_fault_check: mmap: %s\n", strerror(errno));
		return 2;
	}

	predict((uintptr_t)base, off, fsize, pages, &pred);
	expect_pmd = pred.pmds + pred.pte_pages; /* faults if PMDs work wherever possible */
	expect_pte = pred.pages;                 /* faults if everything fell back */

	if (!strcmp(expect, "pmd") && pred.pmds == 0) {
		fprintf(stderr,
			"pmd_fault_check: %s: no 2 MiB region of this mapping can take a PMD fault "
			"(va=%p off=%#" PRIx64 " len=%#" PRIx64 " size=%#" PRIx64 "); nothing to verify\n",
			path, (void *)base, off, len, fsize);
		return 2;
	}

	/* Fault in the measurement code and the rusage path before measuring */
	(void)fault_count();
	{
		unsigned char *scratch = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
					      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (scratch == MAP_FAILED) {
			perror("mmap (scratch)");
			return 2;
		}
		touch(scratch, 1, write);
		munmap(scratch, page_size);
	}

	before = fault_count();
	(void)touch(base, pages, write);
	after = fault_count();
	delta = after - before;

	/*
	 * When no 2 MiB region can take a PMD the two predictions coincide,
	 * so test for the all-PTE outcome first; "pmd" needs at least one
	 * region that could have been, and was, mapped with a PMD.
	 */
	if (delta < (long)expect_pmd)
		verdict = "undercount"; /* fewer faults than any model predicts */
	else if (pred.pmds == 0 || delta + slack >= (long)expect_pte)
		verdict = "pte";
	else if (delta <= (long)expect_pmd + slack)
		verdict = "pmd";
	else
		verdict = "mixed";

	rc = strcmp(verdict, expect) ? 1 : 0;
	result = rc ? "FAIL" : "PASS";

	printf("pmd_fault_check: %s ino=%ju size=%#" PRIx64 " off=%#" PRIx64 " len=%#" PRIx64
	       " va=%p%s mode=%s pages=%lu pmd_possible=%lu pte_only=%lu"
	       " faults=%ld expect_if_pmd=%lu expect_if_pte=%lu verdict=%s expect=%s %s\n",
	       path, (uintmax_t)st.st_ino, fsize, off, len, (void *)base,
	       misalign ? "(misaligned)" : "", write ? "write" : "read",
	       pred.pages, pred.pmds, pred.pte_pages,
	       delta, expect_pmd, expect_pte, verdict, expect, result);

	if (verbose) {
		printf("  va 2MiB aligned: %s; file offset 2MiB aligned: %s; "
		       "size is 2MiB multiple: %s\n",
		       ((uintptr_t)base & PMD_MASK) ? "no" : "yes",
		       (off & PMD_MASK) ? "no" : "yes",
		       (fsize & PMD_MASK) ? "no" : "yes");
	}

	munmap(base, len);
	close(fd);
	return rc;
}
