#ifndef TAGFS_IOCTL_H
#define TAGFS_IOCTL_H

#define TAGFS_MAX_EXTENTS 2

enum tagfs_ext_type {
	TAGFS_EXTENT,      /* Struct is a simple extent */
	TAGFS_INTERLEAVE,  /* Struct is an interleaved extent */
};



struct tagfs_user_extent {
#if 1
/* Start out HPA based */
	u64 hpa;
#else
	char uuid[16];
	size_t offset;
#endif
	size_t len;
};


struct tagfs_ioc_map {
	size_t                    file_size;
	size_t                    ext_list_count;
	struct tagfs_user_extent *ext_list;
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

#define MCIOC_MAP_CREATE    _IOWR(MCIOC_MAGIC, 1, struct tagfs_ioc_map)

#endif /* TAGFS_IOCTL_H */
