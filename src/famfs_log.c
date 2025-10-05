/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2023-2025 Micron Technology, Inc.  All rights reserved.
 */


#include "famfs_log.h"
#include <stdio.h>
#include <stdbool.h>
#include <syslog.h>
#include <stdarg.h>

static unsigned int famfs_log_level = FAMFS_LOG_NOTICE;

static bool to_syslog = true;

static void default_log_func(
	enum famfs_log_level level,
	const char *fmt, va_list ap)
{
	if (to_syslog && level <= famfs_log_level)
		vsyslog(level, fmt, ap);
}

static famfs_log_func_t log_func = default_log_func;

void famfs_set_log_func(famfs_log_func_t func)
{
	if (!func)
		func = default_log_func;

	log_func = func;
}

void famfs_log(enum famfs_log_level level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_func(level, fmt, ap);
	va_end(ap);
}

void famfs_log_set_level(unsigned int def_level)
{
	famfs_log_level = def_level;
}

void famfs_log_enable_syslog(const char *ident, int option, int facility)
{
	to_syslog = true;
	openlog(ident, option, facility);
}

void famfs_log_disable_syslog(void)
{
	to_syslog = false;
}

void famfs_log_close_syslog(void)
{
	closelog();
}

