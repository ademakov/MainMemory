/*
 * util.c - MainMemory utilities.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "util.h"

#include "dlmalloc/malloc.h"

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if ENABLE_TRACE
static __thread int mm_trace_level = 0;
#endif

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
 * Logging Routines.
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

void
mm_print(const char *restrict msg, ...)
{
#if ENABLE_TRACE
	mm_printf("%*s", mm_trace_level * 2, "");
#endif

	va_list va;
	va_start(va, msg);
	mm_vprintf(msg, va);
	va_end(va);

	mm_newline();
}

void
mm_error(int error, const char *restrict msg, ...)
{
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
		mm_fatal(errno, "Error allocating %zu bytes of memory", size);
	}
	return ptr;
}

void *
mm_realloc(void *ptr, size_t size)
{
	ptr = dlrealloc(ptr, size);
	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "Error allocating %zu bytes of memory", size);
	}
	return ptr;
}

void *
mm_calloc(size_t count, size_t size)
{
	void *ptr = dlcalloc(count, size);
	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "Error allocating %zu bytes of memory", count * size);
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
		mm_fatal(errno, "Error allocating %zu bytes of memory", new_amount);
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

void
mm_free(void *ptr)
{
	dlfree(ptr);
}

/**********************************************************************
 * Debug & Trace Utilities.
 **********************************************************************/

void
mm_abort(const char *file, int line, const char *func,
	 const char *restrict msg, ...)
{
#if ENABLE_TRACE
	mm_printf("%*s%s(%s:%d): ", mm_trace_level * 2, "", func, file, line);
#else
	mm_printf("%s(%s:%d): ", func, file, line);
#endif

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
#if ENABLE_TRACE
	mm_printf("%*s%s(%s:%d): ", mm_trace_level * 2, "", func, file, line);
#else
	mm_printf("%s(%s:%d): ", func, file, line);
#endif

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
	if (level < 0)
		mm_trace_level += level;

	mm_printf("%*s%s(%s:%d): ", mm_trace_level * 2, "", func, file, line);

	va_list va;
	va_start(va, msg);
	mm_vprintf(msg, va);
	va_end(va);

	mm_newline();

	if (level > 0)
		mm_trace_level += level;
}

#endif
