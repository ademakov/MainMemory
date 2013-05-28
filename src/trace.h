/*
 * trace.h - MainMemory debug & trace.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#ifndef TRACE_H
#define TRACE_H

#include "config.h"
#include "exit.h"

#if ENABLE_DEBUG
# define ASSERT(e)	(likely(e) ? (void)0 : mm_abort(__FILE__, __LINE__, __FUNCTION__, "failed assertion: %s", #e))
# define DEBUG(...)	mm_debug(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
# define ASSERT(e)	((void) 0)
# define DEBUG(...)	((void) 0)
#endif

#if ENABLE_TRACE
# define TRACE(...)	mm_trace(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
# define ENTER()	mm_trace(+1, __FILE__, __LINE__, __FUNCTION__, "enter")
# define LEAVE()	mm_trace(-1, __FILE__, __LINE__, __FUNCTION__, "leave")
#else
# define TRACE(...)	((void) 0)
# define ENTER()	((void) 0)
# define LEAVE()	((void) 0)
#endif

void mm_where(const char *file, int line, const char *func);

#if ENABLE_DEBUG
void mm_debug(const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 4, 5)))
	__attribute__((nonnull(4)));
#else
#define mm_debug(...)	((void) 0)
#endif

#if ENABLE_TRACE
void mm_trace_prefix(void);
void mm_trace(int level, const char *file, int line, const char *func,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 5, 6)))
	__attribute__((nonnull(4)));
#else
#define mm_trace_prefix()	((void) 0)
#define mm_trace(...)		((void) 0)
#endif

#endif /* TRACE_H */
