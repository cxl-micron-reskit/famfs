#ifndef TAGFS_INTERNAL_H
#define TAGFS_INTERNAL_H

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


#endif
