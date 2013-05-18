/*
 * util.h - MainMemory utilities.
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

#ifndef UTIL_H
#define UTIL_H

#include "common.h"

/**********************************************************************
 * Exit Handling.
 **********************************************************************/

void mm_atexit(void (*func)(void));

void mm_exit(int status)
	__attribute__((noreturn));

/**********************************************************************
 * Logging Routines.
 **********************************************************************/

void mm_flush(void);

void mm_print(const char *restrict msg, ...)
	__attribute__((format(printf, 1, 2)))
	__attribute__((nonnull(1)));

void mm_error(int error, const char *restrict msg, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(2)));

void mm_fatal(int error, const char *restrict msg, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(2)))
	__attribute__((noreturn));

/**********************************************************************
 * Memory Allocation Routines.
 **********************************************************************/

char * mm_strdup(const char *s);

char * mm_asprintf(const char *restrict fmt, ...)
	__attribute__((format(printf, 1, 2)))
	__attribute__((nonnull(1)));

/**********************************************************************
 * Debug & Trace Utilities.
 **********************************************************************/

#if ENABLE_DEBUG
# define ASSERT(e)	(likely(e) ? (void)0 : mm_abort(__FILE__, __LINE__, __FUNCTION__, "failed assertion: %s", #e))
#else
# define ASSERT(e)	((void)0)
#endif

#define ABORT()		mm_abort(__FILE__, __LINE__, __FUNCTION__, "ABORT")

#if ENABLE_DEBUG
# define DEBUG(...)	mm_debug(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
# define DEBUG(...)	((void)0)
#endif

#if ENABLE_TRACE
# define TRACE(...)	mm_trace(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
# define ENTER()	mm_trace(+1, __FILE__, __LINE__, __FUNCTION__, "enter")
# define LEAVE()	mm_trace(-1, __FILE__, __LINE__, __FUNCTION__, "leave")
#else
# define TRACE(...)	((void)0)
# define ENTER()	((void)0)
# define LEAVE()	((void)0)
#endif

void mm_abort(const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 4, 5)))
	__attribute__((nonnull(4)))
	__attribute__((noreturn));

#if ENABLE_DEBUG
void mm_debug(const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 4, 5)))
	__attribute__((nonnull(4)));
#endif

#if ENABLE_TRACE
void mm_trace(int level, const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 5, 6)))
	__attribute__((nonnull(4)));
#endif

#endif /* UTIL_H */
