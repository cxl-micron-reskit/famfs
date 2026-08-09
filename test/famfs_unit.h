/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

#ifndef FAMFS_UNIT_H
#define FAMFS_UNIT_H

int create_mock_famfs_instance(const char *path, u64 device_size,
			       struct famfs_superblock **sb_out,
			       struct famfs_log **log_out);

/* Like create_mock_famfs_instance(), but with a caller-specified allocation
 * unit (4096 or FAMFS_ALLOC_UNIT). The plain version above uses FAMFS_ALLOC_UNIT. */
int create_mock_famfs_instance_au(const char *path, u64 device_size, u64 alloc_unit,
				  struct famfs_superblock **sb_out,
				  struct famfs_log **log_out);

#endif
