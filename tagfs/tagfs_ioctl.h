#ifndef TAGFS_IOCTL_H
#define TAGFS_IOCTL_H

#define TAGFS_MAX_EXTENTS 2

/* move to private ************************************************************/
/* This is tne internal metadata for the backing memory of a tagfs file
 */
struct tagfs_internal_extent {
	u64     hpa;
	size_t  len;
};
/*
 * Each mcache map file has this hanging from its inode->i_private.
 */
struct tagfs_file_meta {
	size_t                        tfs_extent_ct;
	struct tagfs_internal_extent  tfs_extents[];  /* flexible array */
};

/* end private ****************************************************************/

enum tagfs_ext_type {
	TAGFS_EXTENT,      /* Struct is a simple extent */
	TAGFS_INTERLEAVE,  /* Struct is an interleaved extent */
};

struct tagfs_user_extent {
	char uuid[16];
	size_t offset;
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
