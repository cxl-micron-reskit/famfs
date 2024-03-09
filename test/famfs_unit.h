/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2024 Micron Technology, Inc.  All rights reserved.
 */

#ifndef FAMFS_UNIT_H
#define FAMFS_UNIT_H

int create_mock_famfs_instance(const char *path, u64 device_size,
			       struct famfs_superblock **sb_out,
			       struct famfs_log **log_out);

#endif
