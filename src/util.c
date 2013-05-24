/*
 * util.c - MainMemory utilities.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "util.h"

#include "hook.h"
#include "log.h"
#include "sched.h"
#include "task.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

/**********************************************************************
 * Exit Handling.
 **********************************************************************/

static struct mm_hook mm_exit_hook;

void
mm_atexit(void (*func)(void))
{
	mm_hook_head_proc(&mm_exit_hook, func);
}

static void
mm_do_atexit(void)
{
	mm_hook_call_proc(&mm_exit_hook, true);
}

void
mm_exit(int status)
{
	mm_do_atexit();
	exit(status);
}

/**********************************************************************
 * Basic Tracing Routines.
 **********************************************************************/

#if ENABLE_TRACE

static int mm_trace_level;

static void
mm_trace_level_add(int level)
{
	if (mm_running_task != NULL)
		mm_running_task->trace_level += level;
	else
		mm_trace_level += level;
}

static void
mm_prefix(void)
{
	if (likely(mm_running_task != NULL)) {
		mm_log_fmt("[%d %s] %*s",
			   mm_task_id(mm_running_task),
			   mm_running_task->name,
			   mm_running_task->trace_level * 2, "");
	} else {
		mm_log_fmt("%*s", mm_trace_level * 2, "");
	}
}

#else

#define mm_prefix() ((void) 0)

#endif

/**********************************************************************
 * Logging Routines.
 **********************************************************************/

void
mm_print(const char *restrict msg, ...)
{
	mm_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");
}

void
mm_error(int error, const char *restrict msg, ...)
{
	mm_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_log_fmt(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}
}

void
mm_fatal(int error, const char *restrict msg, ...)
{
	mm_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_print(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}

	mm_log_str("exiting...\n");
	mm_log_flush();

	mm_exit(EXIT_FAILURE);
}

/**********************************************************************
 * Memory Allocation Routines.
 **********************************************************************/

#include "alloc.h"

char *
mm_strdup(const char *s)
{
	size_t len = strlen(s) + 1;
	return memcpy(mm_alloc(len), s, len);
}

char *
mm_asprintf(const char *restrict fmt, ...)
{
	int len;
	va_list va;
	char dummy[1];

	va_start(va, fmt);
	len = vsnprintf(dummy, sizeof dummy, fmt, va);
	va_end(va);

	if (unlikely(len < 0)) {
		mm_fatal(errno, "invalid format string");
	}

	char *ptr = mm_alloc(++len);

	va_start(va, fmt);
	vsnprintf(ptr, len, fmt, va);
	va_end(va);

	return ptr;
}

/**********************************************************************
 * Debug & Trace Utilities.
 **********************************************************************/

static void
mm_location(const char *file, int line, const char *func)
{
	mm_prefix();
	mm_log_fmt("%s(%s:%d): ", func, file, line);
}

void
mm_abort(const char *file, int line, const char *func,
	 const char *restrict msg, ...)
{
	mm_location(file, line, func);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\naborting...\n");
	mm_log_flush();

	mm_do_atexit();
	abort();
}

#if ENABLE_DEBUG

void
mm_debug(const char *file, int line, const char *func,
	 const char *restrict msg, ...)
{
	mm_location(file, line, func);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");
}

#endif

#if ENABLE_TRACE

void
mm_trace(int level, const char *file, int line, const char *func, 
	 const char *restrict msg, ...)
{
	if (level < 0) {
		mm_trace_level_add(level);
	}

	mm_location(file, line, func);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");

	if (level > 0) {
		mm_trace_level_add(level);
	}
}

#endif
