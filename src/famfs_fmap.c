// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2024 Micron Technology, Inc.  All rights reserved.
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/ioctl.h>
#include <linux/uuid.h> /* Our preferred UUID format */
#include <uuid/uuid.h>  /* for uuid_generate / libuuid */
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/user.h>
#include <sys/param.h> /* MIN()/MAX() */
#include <libgen.h>
#include <sys/mount.h>

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/famfs_ioctl.h>

#include "famfs_meta.h"
#include "famfs_fmap.h"

#if 0
int
copy_to_cursor(
	void **dest,
	void *src,
	uint count,
	size_t len)
{
	size_t size = count * len;

	assert(size >= 0);
	if (size == 0)
		return 0;

	memcpy(*dest, src, size);
	*dest += size;
}

ssize_t
write_fmap_to_buf(
	const struct fmap_log_header *fmap,
	void *buf,
	size_t buflen)
{
	struct fmap_log_header *fmh = buf;

	memset(buf, 0, buflen);

	fmh->struct_tag = LOG_HEADER_TAG;
	fmh->fmap_log_version = FAMFS_LOG_VERSION;
	fmh->fmap_ext_type = fmap->fmap_ext_type;
}
#endif

static ssize_t
get_simple_ext_list(
	const void *buf,
	ssize_t buf_remainder,
	size_t next,
	struct fmap_simple_ext **se_out)
{
	ssize_t se_size = next * sizeof(*se_out);
	struct fmap_simple_ext *se;

	if (next > FAMFS_MAX_SIMPLE_EXT)
		return -EINVAL;

	if (se_size > buf_remainder)
		return -EINVAL;

	se = calloc(next, sizeof(*se));
	if (!se)
		return -ENOMEM;

	memcpy(&se, buf, se_size);
	*se_out = se;

	return se_size;
}


static ssize_t
get_interleaved_ext_list(
	void *buf,
	ssize_t buflen,
	struct fmap_mem_iext **ie_out)
{
	ssize_t ie_size = 0;
	struct fmap_simple_ext *se = NULL;
	struct fmap_mem_iext *local_ie = NULL;
	ssize_t buf_remainder = buflen;
	void *localbuf = buf;
	ssize_t se_size;
	int rc;

	ie_size = local_ie->iext.ie_nstrips * sizeof(*se);

	if (ie_size > buf_remainder)
		return -EINVAL;

	/* Allocate a header */
	local_ie = calloc(local_ie->iext.ie_nstrips, sizeof(*local_ie));
	if (!local_ie)
		return -ENOMEM;

	memcpy(local_ie, buf, ie_size);

	if (local_ie->iext.struct_tag != LOG_IEXT_TAG) {
		rc = -EINVAL;
		goto err_out;
	}

	if (local_ie->iext.reserved != 0) {
		rc = -EINVAL;
		goto err_out;
	}

	/* Move past header */
	localbuf += ie_size;
	buf_remainder -= ie_size;

	if (local_ie->iext.ie_nstrips > FAMFS_MAX_STRIPS) {
		rc = -EINVAL;
		goto err_out;
	}

	se_size = get_simple_ext_list(localbuf, buf_remainder,
				      local_ie->iext.ie_nstrips, &se);
	if (se_size <= 0) {
		if (se_size < 0)
			rc = se_size;
		else
			rc = -EFAULT;
		goto err_out;
	}

	local_ie->se = se;
	*ie_out = local_ie;
	return ie_size + se_size;

err_out:
	if (se)
		free(se);
	if (local_ie)
		free(local_ie);
	return rc;
}

/*
 * Allocate an in-memory fmap struct and read read a message-format
 * fmap into it
 */
ssize_t
read_fmap_from_buf(
	struct fmap_mem_header *fmap_out,
	void *buf,
	size_t buflen)
{
	struct fmap_mem_header *lfmap = NULL;
	ssize_t buf_remainder = buflen;
	void *localbuf = buf;
	int rc;

	lfmap = calloc(1, sizeof(struct fmap_mem_header));
	if (!lfmap) {
		rc = -ENOMEM;
		goto err_out;
	}
	memcpy(&lfmap->flh, buf, sizeof(lfmap->flh));

	if (lfmap->flh.struct_tag != LOG_HEADER_TAG) {
		rc = -EINVAL;
		goto err_out;
	}

	if (lfmap->flh.fmap_log_version != FAMFS_LOG_VERSION) {
		rc =-EINVAL;
		goto err_out;
	}

	if (lfmap->flh.reserved != 0) {
		rc =-EINVAL;
		goto err_out;
	}

	buf_remainder -= sizeof(*lfmap);
	localbuf +=  sizeof(*lfmap);

	switch (lfmap->flh.fmap_ext_type) {
	case FAMFS_EXT_SIMPLE: {
		struct fmap_simple_ext *se = NULL;
		ssize_t se_size = 0;;

		rc = get_simple_ext_list(localbuf, buf_remainder,
					 lfmap->flh.next, &se);
		if (rc)
			goto err_out;

		buf_remainder -= se_size;
		localbuf += se_size;
		break;
	}
	case FAMFS_EXT_INTERLEAVE: {
		/* TODO: for loop over lfmap->ie.next */
		struct fmap_mem_iext *ie = NULL;
		ssize_t ie_size = 0;

		ie_size = get_interleaved_ext_list(localbuf, buf_remainder,
					      &ie);
		if (ie_size) {
			if (ie_size < 0)
				rc = ie_size;
			else
				rc = -EINVAL;
			goto err_out;
		}

		buf_remainder -= ie_size;
		localbuf += ie_size;
		break;
	}
	}

err_out:
if (lfmap)
		free(lfmap);
	return rc;
}
