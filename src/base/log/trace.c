/*
 * base/log/trace.c - MainMemory trace utilities.
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

#include "base/log/trace.h"
#include "base/log/debug.h"
#include "base/log/log.h"
#include "base/thr/thread.h"
#include "base/util/format.h"

/**********************************************************************
 * Trace Context.
 **********************************************************************/

#if ENABLE_TRACE

static struct mm_trace_context *
mm_trace_getcontext_default(void)
{
	struct mm_thread *thread = mm_thread_self();
	if (unlikely(thread == NULL))
		ABORT();
	return mm_thread_gettracecontext(thread);
}

static mm_trace_getcontext_t mm_trace_getcontext = mm_trace_getcontext_default;

void
mm_trace_set_getcontext(mm_trace_getcontext_t getcontext)
{
	if (getcontext == NULL)
		mm_trace_getcontext = mm_trace_getcontext_default;
	else
		mm_trace_getcontext = getcontext;
}

void
mm_trace_context_prepare(struct mm_trace_context *context,
			 const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	context->owner = mm_vformat(&mm_global_arena, fmt, va);
	va_end(va);

	context->level = 0;
	context->recur = 0;
}

void
mm_trace_context_cleanup(struct mm_trace_context *context)
{
	mm_arena_free(&mm_global_arena, context->owner);
}

#endif

/**********************************************************************
 * Trace Level.
 **********************************************************************/

#if ENABLE_TRACE

static bool
mm_trace_enter(int level)
{
	struct mm_trace_context *context = mm_trace_getcontext();
	if (unlikely(context->recur))
		return false;

	if (level < 0)
		context->level += level;
	context->recur++;

	return true;
}

static void
mm_trace_leave(int level)
{
	struct mm_trace_context *context = mm_trace_getcontext();

	if (level > 0)
		context->level += level;
	context->recur--;
}

#endif

/**********************************************************************
 * Trace Utilities.
 **********************************************************************/

void
mm_where(const char *restrict location, const char *function)
{
	mm_trace_prefix();
	mm_log_fmt("%s(%s): ", function, location);
}

#if ENABLE_TRACE

void
mm_trace_prefix(void)
{
	struct mm_trace_context *context = mm_trace_getcontext();
	mm_log_fmt("%s %*s", context->owner, context->level * 2, "");
}

void
mm_trace(int level,
	 const char *restrict location,
	 const char *restrict function, 
	 const char *restrict msg, ...)
{
	if (!mm_trace_enter(level))
		return;

	mm_where(location, function);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");

	mm_trace_leave(level);
}

#endif
