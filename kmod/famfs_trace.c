/* SPDX-License-Identifier: GPL-2.0 */
/*
 * famfs - dax file system for shared fabric-attached memory
 *
 * Copyright 2023 Micron Technology, Inc.
 */

#include "famfs.h"
#include "famfs_internal.h"
#include "famfs_ioctl.h"

#define CREATE_TRACE_POINTS
#include "famfs_trace.h"
