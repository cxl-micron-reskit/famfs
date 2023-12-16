/* SPDX-License-Identifier: GPL-2.0 */
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023 Micron Technology, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM famfs

#if !defined(_TRACE_FAMFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FAMFS_H

#include <linux/tracepoint.h>

typedef unsigned long long	famfs_ino_t;	/* <inode> type */

TRACE_EVENT(famfs_meta_to_dax_offset,
	    TP_PROTO(struct inode *ip, loff_t offset, loff_t len, loff_t dax_ofs, u64 dax_len),
	    TP_ARGS(ip, offset, len, dax_ofs, dax_len),
	    TP_STRUCT__entry(
		__field(famfs_ino_t, ino)
		__field(loff_t, offset)
		__field(loff_t, len)
		__field(loff_t, dax_ofs)
		__field(u64, dax_len)
	    ),
	    TP_fast_assign(
		__entry->ino = ip->i_ino;
		__entry->offset = offset;
		__entry->len = len;
		__entry->dax_ofs = dax_ofs;
		__entry->dax_len = dax_len;
	    ),
	    TP_printk("ino 0x%llx ofs %llx len 0x%llx dax_ofs 0x%llx dax_len 0x%llx",
		      __entry->ino, __entry->offset, __entry->len,
		      __entry->dax_ofs, __entry->dax_len)
)

TRACE_EVENT(famfs_filemap_fault,
	TP_PROTO(struct inode *ip, enum page_entry_size pe_size,
		 bool write_fault),
	TP_ARGS(ip, pe_size, write_fault),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(famfs_ino_t, ino)
		__field(enum page_entry_size, pe_size)
		__field(bool, write_fault)
	),
	TP_fast_assign(
		__entry->dev = ip->i_sb->s_dev;
		__entry->ino = ip->i_ino;
		__entry->pe_size = pe_size;
		__entry->write_fault = write_fault;
	),
	TP_printk("dev %d:%d ino 0x%llx %s write_fault %d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino,
		  __print_symbolic(__entry->pe_size,
			{ PE_SIZE_PTE,	"PTE" },
			{ PE_SIZE_PMD,	"PMD" },
			{ PE_SIZE_PUD,	"PUD" }),
		  __entry->write_fault)
)

#endif

#undef TRACE_INCLUDE_PATH
/* FAMFS_TRACE_PATH must be provided by the Makefile in CFLAGS */
#define TRACE_INCLUDE_PATH FAMFS_TRACE_PATH
#define TRACE_INCLUDE_FILE famfs_trace

#include <trace/define_trace.h>
