/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2025-2026 Micron Technology, Inc.  All rights reserved.
 */

#ifndef _H_FAMFS_REST
#define _H_FAMFS_REST

int famfs_diag_server_listen(const char *shadow_root);
int famfs_diag_server_start(const char *shadow_root);
void famfs_diag_server_stop(void);

#endif /* _H_FAMFS_REST */
