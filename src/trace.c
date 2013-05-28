/*
 * trace.c - MainMemory debug & trace.
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

#include "trace.h"

#include "log.h"
#include "sched.h"
#include "task.h"

#include <stdarg.h>

/**********************************************************************
 * Trace Level.
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

void
mm_trace_prefix(void)
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


#endif

/**********************************************************************
 * Debug & Trace Utilities.
 **********************************************************************/

void
mm_where(const char *file, int line, const char *func)
{
	mm_trace_prefix();
	mm_log_fmt("%s(%s:%d): ", func, file, line);
}

#if ENABLE_DEBUG

void
mm_debug(const char *file, int line, const char *func,
	 const char *restrict msg, ...)
{
	mm_where(file, line, func);

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

	mm_where(file, line, func);

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
