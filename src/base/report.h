/*
 * base/report.h - MainMemory message logging.
 *
 * Copyright (C) 2012-2019  Aleksey Demakov
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

#ifndef BASE_REPORT_H
#define BASE_REPORT_H

#include "common.h"

/**********************************************************************
 * Message verbosity control.
 **********************************************************************/

void
mm_set_verbosity_level(int level);

void
mm_set_verbose_enabled(bool value);

void
mm_set_warning_enabled(bool value);

int
mm_get_verbosity_level(void);

bool
mm_get_verbose_enabled(void);

bool
mm_get_warning_enabled(void);

/**********************************************************************
 * Plain info messages.
 **********************************************************************/

void NONNULL(1) FORMAT(1, 2)
mm_brief(const char *restrict msg, ...);

void NONNULL(1) FORMAT(1, 2)
mm_verbose(const char *restrict msg, ...);

void NONNULL(1) FORMAT(1, 2)
mm_verbose2(const char *restrict msg, ...);

void NONNULL(1) FORMAT(1, 2)
mm_verbose3(const char *restrict msg, ...);

/**********************************************************************
 * Error messages.
 **********************************************************************/

void NONNULL(2) FORMAT(2, 3)
mm_warning(int error, const char *restrict msg, ...);

void NONNULL(2) FORMAT(2, 3)
mm_error(int error, const char *restrict msg, ...);

void NONNULL(2) FORMAT(2, 3) NORETURN
mm_fatal(int error, const char *restrict msg, ...);

/**********************************************************************
 * Debug messages.
 **********************************************************************/

#define ABORT()		mm_abort_with_message(__LOCATION__, __FUNCTION__, "ABORT")

#define VERIFY(e)	(likely(e) ? (void) 0 :	\
			 mm_abort_with_message(__LOCATION__, __FUNCTION__, "failed condition: %s", #e))

#if ENABLE_DEBUG
# define ASSERT(e)	VERIFY(e)
# define DEBUG(...)	mm_debug(__LOCATION__, __FUNCTION__, __VA_ARGS__)
#else
# define ASSERT(e)	((void) 0)
# define DEBUG(...)	((void) 0)
#endif

void NONNULL(1, 2, 3) FORMAT(3, 4) NORETURN
mm_abort_with_message(const char *restrict location,
		      const char *restrict function,
		      const char *restrict msg, ...);

#if ENABLE_DEBUG
void NONNULL(1, 2, 3) FORMAT(3, 4)
mm_debug(const char *restrict location,
	 const char *restrict function,
	 const char *restrict msg, ...);
#else
# define mm_debug(...)	((void) 0)
#endif

/**********************************************************************
 * Trace messages.
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

#if ENABLE_TRACE
void NONNULL(2, 3, 4) FORMAT(4, 5)
mm_trace(int level,
	 const char *restrict location,
	 const char *restrict function,
	 const char *restrict msg, ...);
#else
#define mm_trace(...)	((void) 0)
#endif /* ENABLE_TRACE */

/**********************************************************************
 * Trace context.
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

void NONNULL(1, 2) FORMAT(2, 3)
mm_trace_context_prepare(struct mm_trace_context *context, const char *restrict fmt, ...);

void NONNULL(1)
mm_trace_context_cleanup(struct mm_trace_context *context);

#endif /* ENABLE_TRACE */

#endif /* BASE_REPORT_H */
