/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */
#ifndef _FAMFS_LOG_H
#define _FAMFS_LOG_H

#include <stdarg.h>
#include <syslog.h>

/**
 * Log severity level
 *
 * These levels correspond to syslog(2) log levels since they are widely used.
 */

enum famfs_log_level {
        FAMFS_LOG_EMERG,
        FAMFS_LOG_ALERT,
        FAMFS_LOG_CRIT,
        FAMFS_LOG_ERR,
        FAMFS_LOG_WARNING,
        FAMFS_LOG_NOTICE,
        FAMFS_LOG_INFO,
        FAMFS_LOG_DEBUG
};

/**
 * Log message handler function.
 *
 * @param level log severity level
 * @param fmt sprintf-style format string including newline
 * @param ap format string arguments
 */
typedef void (*famfs_log_func_t)(enum famfs_log_level level,
                                const char *fmt, va_list ap);
void famfs_set_log_func(famfs_log_func_t func);
void famfs_log_set_level(int def_level);
int famfs_log_get_level(void);
void famfs_log(enum famfs_log_level level, const char *fmt, ...);
void famfs_log_enable_syslog(const char *ident, int option, int facility);
void famfs_log_disable_syslog(void);
void famfs_log_close_syslog(void);
const char *famfs_log_level_string(int level);

#endif /* _FAMFS_LOG_H */

