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

char *famfs_log_level_strings[] = {
	"FAMFS_LOG_EMERG",
	"FAMFS_LOG_ALERT",
	"FAMFS_LOG_CRIT",
	"FAMFS_LOG_ERR",
	"FAMFS_LOG_WARNING",
	"FAMFS_LOG_NOTICE",
	"FAMFS_LOG_INFO",
	"FAMFS_LOG_DEBUG",
};

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

static int
log_level_valid(int level)
{
	if (level < FAMFS_LOG_EMERG || level > FAMFS_LOG_DEBUG)
		return false;
	return true;
}
			

void famfs_log_set_level(int level)
{
	if (!log_level_valid(level)) {
		famfs_log(FAMFS_LOG_ERR, "%s: invalid log level %d\n",
			  __func__);
		return;
	}
	famfs_log_level = level;
}

int famfs_log_get_level(void)
{
	return famfs_log_level;
}

const char *
famfs_log_level_string(int level)
{
	if (log_level_valid(level))
		return famfs_log_level_strings[level];

	return "invalid log level";
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

