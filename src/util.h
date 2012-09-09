/*
 * util.h - MainMemory utilities.
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

#ifndef MM_UTIL_H
#define MM_UTIL_H

#include <config.h>
#include <stddef.h>

#ifdef __GNUC__
# define likely(x)	__builtin_expect(!!(x), 1)
# define unlikely(x)	__builtin_expect(!!(x), 0)
#else
# define likely(x)	(x)
# define unlikely(x)	(x)
#endif

void mm_print(const char *restrict msg, ...);
void mm_flush(void);

void mm_error(int error, const char *restrict msg, ...)
	__attribute__((format(printf, 2, 3)));

void mm_fatal(int error, const char *restrict msg, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((noreturn));

#define ABORT() mm_abort(__FILE__, __LINE__, __FUNCTION__, "ABORT")

#if ENABLE_DEBUG
# define ASSERT(e)	(likely(e) ? (void)0 : mm_abort(__FILE__, __LINE__, __FUNCTION__, "failed assertion: %s", #e))
#else
# define ASSERT(e)	((void)0)
#endif

void mm_abort(const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 4, 5)))
	__attribute__((noreturn));

#if ENABLE_TRACE
# define TRACE(...)	mm_trace(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
# define ENTER()	mm_trace(__FILE__, __LINE__, __FUNCTION__, "enter"), mm_trace_enter()
# define LEAVE()	mm_trace_leave(), mm_trace(__FILE__, __LINE__, __FUNCTION__, "leave")
#else
# define TRACE(...)	((void)0)
# define ENTER()	((void)0)
# define LEAVE()	((void)0)
#endif

#if ENABLE_TRACE
void mm_trace_enter(void);
void mm_trace_leave(void);
void mm_trace(const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 4, 5)));
#endif

void * mm_alloc(size_t size);
void * mm_realloc(void *ptr, size_t size);
void * mm_calloc(size_t count, size_t size);
void * mm_crealloc(void *ptr, size_t old_count, size_t new_count, size_t size);
void mm_free(void *ptr);

#endif
