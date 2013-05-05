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

#include "sched.h"
#include "task.h"

#include "dlmalloc/malloc.h"

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**********************************************************************
 * Exit Handling.
 **********************************************************************/

struct mm_exit_rec
{
	struct mm_exit_rec *next;
	void (*func)(void);
};

static struct mm_exit_rec *mm_exit_list;

void
mm_atexit(void (*func)(void))
{
	ASSERT(func != NULL);

	struct mm_exit_rec *rec = mm_alloc(sizeof(struct mm_exit_rec));
	rec->func = func;
	rec->next = mm_exit_list;
	mm_exit_list = rec;
}

static void
mm_do_atexit(void)
{
	while (mm_exit_list != NULL) {
		struct mm_exit_rec *rec = mm_exit_list;
		mm_exit_list = mm_exit_list->next;
		(rec->func)();
		mm_free(rec);
	}
}

void
mm_exit(int status)
{
	mm_do_atexit();
	exit(status);
}

/**********************************************************************
 * Basic Logging Routines.
 **********************************************************************/

static void
mm_vprintf(const char *restrict msg, va_list va)
{
	vfprintf(stderr, msg, va);
}

static void
mm_printf(const char *restrict msg, ...)
{
	va_list va;
	va_start(va, msg);
	vfprintf(stderr, msg, va);
	va_end(va);
}

static void
mm_newline(void)
{
	fprintf(stderr, "\n");
}

void
mm_flush(void)
{
	fflush(stderr);
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
		mm_printf("[%d][%s] %*s",
			  mm_task_id(mm_running_task),
			  mm_running_task->name,
			  mm_running_task->trace_level * 2, "");
	} else {
		mm_printf("%*s", mm_trace_level * 2, "");
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
	mm_vprintf(msg, va);
	va_end(va);

	mm_newline();
}

void
mm_error(int error, const char *restrict msg, ...)
{
	mm_prefix();

	va_list va;
	va_start(va, msg);
	mm_vprintf(msg, va);
	va_end(va);

	if (error) {
		mm_printf(": %s", strerror(error));
	}

	mm_newline();
}

void
mm_fatal(int error, const char *restrict msg, ...)
{
	mm_prefix();

	va_list va;
	va_start(va, msg);
	mm_vprintf(msg, va);
	va_end(va);

	if (error) {
		mm_print(": %s", strerror(error));
	}

	mm_newline();

	mm_printf("exiting...");
	mm_newline();
	mm_flush();

	mm_exit(EXIT_FAILURE);
}

/**********************************************************************
 * Memory Allocation Routines.
 **********************************************************************/

void *
mm_alloc(size_t size)
{
	void *ptr = dlmalloc(size);
	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	}
	return ptr;
}

void *
mm_realloc(void *ptr, size_t size)
{
	ptr = dlrealloc(ptr, size);
	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	}
	return ptr;
}

void *
mm_calloc(size_t count, size_t size)
{
	void *ptr = dlcalloc(count, size);
	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	}
	return ptr;
}

void *
mm_crealloc(void *ptr, size_t old_count, size_t new_count, size_t size)
{
	ASSERT(old_count < new_count);
	size_t old_amount = old_count * size;
	size_t new_amount = new_count * size;
	ptr = dlrealloc(ptr, new_amount);
	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", new_amount);
	}
	memset(ptr + old_amount, 0, new_amount - old_amount);
	return ptr;
}

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

void
mm_free(void *ptr)
{
	dlfree(ptr);
}

/**********************************************************************
 * Debug & Trace Utilities.
 **********************************************************************/

static void
mm_location(const char *file, int line, const char *func)
{
	mm_prefix();
	mm_printf("%s(%s:%d): ", func, file, line);
}

void
mm_abort(const char *file, int line, const char *func,
	 const char *restrict msg, ...)
{
	mm_location(file, line, func);

	va_list va;
	va_start(va, msg);
	mm_vprintf(msg, va);
	va_end(va);

	mm_newline();

	mm_printf("aborting...");
	mm_newline();
	mm_flush();

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
	mm_vprintf(msg, va);
	va_end(va);

	mm_newline();
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
	mm_vprintf(msg, va);
	va_end(va);

	mm_newline();

	if (level > 0) {
		mm_trace_level_add(level);
	}
}

#endif
