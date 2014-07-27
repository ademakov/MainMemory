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

#include "core.h"
#include "log.h"
#include "task.h"
#include "thread.h"

#include <stdarg.h>

/**********************************************************************
 * Trace Level.
 **********************************************************************/

#if ENABLE_TRACE

/* Trace nesting level. */
static __thread int mm_trace_level;

/* Trace recursion detection. */
static __thread int mm_trace_recur;

static bool
mm_trace_enter(int level)
{
	struct mm_task *task = mm_core != NULL ? mm_core->task : NULL;
	if (task != NULL) {
		if (unlikely(task->trace_recur))
			return false;
		if (level < 0)
			task->trace_level += level;
		task->trace_recur++;
	} else {
		if (unlikely(mm_trace_recur)) 
			return false;
		if (level < 0)
			mm_trace_level += level;
		mm_trace_recur++;
	}
	return true;
}

static void
mm_trace_leave(int level)
{
	struct mm_task *task = mm_core != NULL ? mm_core->task : NULL;
	if (task != NULL) {
		if (level > 0)
			task->trace_level += level;
		task->trace_recur--;
	} else {
		if (level > 0)
			mm_trace_level += level;
		mm_trace_recur--;
	}
}

void
mm_trace_prefix(void)
{
	struct mm_task *task = mm_core != NULL ? mm_core->task : NULL;
	if (task != NULL) {
		mm_log_fmt("[%s][%d %s] %*s",
			   mm_thread_getname(mm_thread_self()),
			   mm_task_getid(task),
			   mm_task_getname(task),
			   task->trace_level * 2, "");
	} else {
		mm_log_fmt("[%s]%*s",
			   mm_thread_getname(mm_thread_self()),
			   mm_trace_level * 2, "");
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
	if (!mm_trace_enter(level))
		return;

	mm_where(file, line, func);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");

	mm_trace_leave(level);
}

#endif
