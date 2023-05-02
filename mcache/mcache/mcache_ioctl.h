/*
 * Copyright (C) 2015-2016 Micron Technology, Inc.  All rights reserved.
 */

#ifndef MCACHE_IOCTL_H
#define MCACHE_IOCTL_H

struct mcioc_map {
	merr_t  im_err;         /* Always copied out (unless EFAULT) */
	size_t  im_bktsz;       /* this parameter is output */

	size_t  im_mbidc;       /* mblock ID count */
	u64    *im_mbidv;       /* mblock ID vector */
};

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define MCIOC_MAGIC 'u'

#define MCIOC_MAP_CREATE    _IOWR(MCIOC_MAGIC, 1, struct mcioc_map)

#endif /* MCACHE_IOCTL_H */
