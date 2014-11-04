/*
 * base/log/trace.h - MainMemory trace utilities.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#ifndef BASE_LOG_TRACE_H
#define BASE_LOG_TRACE_H

#include "common.h"

/**********************************************************************
 * Trace Utilities.
 **********************************************************************/

#if ENABLE_TRACE
# define TRACE(...)	mm_trace(0, __LOCATION__, __FUNCTION__, __VA_ARGS__)
# define ENTER()	mm_trace(+1, __LOCATION__, __FUNCTION__, "enter")
# define LEAVE()	mm_trace(-1, __LOCATION__, __FUNCTION__, "leave")
#else
# define TRACE(...)	((void) 0)
# define ENTER()	((void) 0)
# define LEAVE()	((void) 0)
#endif

void mm_where(const char *restrict location, const char *restrict function)
	__attribute__((nonnull(1, 2)));

#if ENABLE_TRACE
void mm_trace_prefix(void);
void mm_trace(int level,
	      const char *restrict location,
	      const char *restrict function,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 4, 5)))
	__attribute__((nonnull(2, 3, 4)));
#else
#define mm_trace_prefix()	((void) 0)
#define mm_trace(...)		((void) 0)
#endif

/**********************************************************************
 * Trace Context.
 **********************************************************************/

#if ENABLE_TRACE

struct mm_trace_context
{
	/* Human-readable description of the owner thread or task. */
	char *owner;

	/* Trace nesting level. */
	int level;

	/* Trace recursion detection (to avoid infinite recursion). */
	int recur;
};

typedef struct mm_trace_context * (*mm_trace_getcontext_t)(void);

void mm_trace_set_getcontext(mm_trace_getcontext_t getcontext);

void mm_trace_context_prepare(struct mm_trace_context *context,
			      const char *restrict fmt, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(1, 2)));

void mm_trace_context_cleanup(struct mm_trace_context *context)
	__attribute__((nonnull(1)));

#endif

#endif /* BASE_LOG_TRACE_H */
