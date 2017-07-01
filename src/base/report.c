/*
 * base/report.c - MainMemory message logging.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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

#include "base/report.h"

#include "base/exit.h"
#include "base/format.h"
#include "base/logger.h"
#include "base/fiber/fiber.h"
#include "base/memory/global.h"
#include "base/thread/thread.h"

#include <stdarg.h>

static bool mm_verbose_enabled = false;
static bool mm_warning_enabled = false;

#if ENABLE_TRACE
static void
mm_trace_prefix(void);
#else
#define mm_trace_prefix()	((void) 0)
#endif /* ENABLE_TRACE */

/**********************************************************************
 * Message verbosity control.
 **********************************************************************/

void
mm_set_verbose_enabled(bool value)
{
	mm_verbose_enabled = value;
}

void
mm_set_warning_enabled(bool value)
{
	mm_warning_enabled = value;
}

bool
mm_get_verbose_enabled(void)
{
	return mm_verbose_enabled;
}

bool
mm_get_warning_enabled(void)
{
	return mm_warning_enabled;
}

/**********************************************************************
 * Plain info messages.
 **********************************************************************/

void NONNULL(1) FORMAT(1, 2)
mm_verbose(const char *restrict msg, ...)
{
	if (!mm_verbose_enabled)
		return;

	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");
}

void NONNULL(1) FORMAT(1, 2)
mm_brief(const char *restrict msg, ...)
{
	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");
}

/**********************************************************************
 * Error messages.
 **********************************************************************/

void NONNULL(2) FORMAT(2, 3)
mm_warning(int error, const char *restrict msg, ...)
{
	if (!mm_warning_enabled)
		return;

	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_log_fmt(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}
}

void NONNULL(2) FORMAT(2, 3)
mm_error(int error, const char *restrict msg, ...)
{
	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_log_fmt(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}
}

void NONNULL(2) FORMAT(2, 3) NORETURN
mm_fatal(int error, const char *restrict msg, ...)
{
	mm_trace_prefix();

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	if (error) {
		mm_log_fmt(": %s\n", strerror(error));
	} else {
		mm_log_str("\n");
	}

	mm_exit(MM_EXIT_FAILURE);
}

/**********************************************************************
 * Location message.
 **********************************************************************/

static void NONNULL(1, 2)
mm_where(const char *restrict location, const char *function)
{
	mm_trace_prefix();
	mm_log_fmt("%s(%s): ", function, location);
}

/**********************************************************************
 * Debug messages.
 **********************************************************************/

void NONNULL(1, 2, 3) FORMAT(3, 4) NORETURN
mm_abort_with_message(const char *restrict location,
		      const char *restrict function,
		      const char *restrict msg, ...)
{
	mm_where(location, function);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_abort();
}

#if ENABLE_DEBUG

void NONNULL(1, 2, 3) FORMAT(3, 4)
mm_debug(const char *restrict location,
	 const char *restrict function,
	 const char *restrict msg, ...)
{
	mm_where(location, function);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");
}

#endif

/**********************************************************************
 * Trace messages.
 **********************************************************************/

#if ENABLE_TRACE

static struct mm_trace_context *
mm_trace_getcontext(void)
{
	struct mm_strand *strand = mm_strand_selfptr();
	if (strand != NULL)
		return &strand->fiber->trace;
	struct mm_thread *thread = mm_thread_selfptr();
	if (unlikely(thread == NULL))
		ABORT();
	return mm_thread_gettracecontext(thread);
}

void NONNULL(1, 2) FORMAT(2, 3)
mm_trace_context_prepare(struct mm_trace_context *context, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	context->owner = mm_vformat(&mm_global_arena, fmt, va);
	va_end(va);

	context->level = 0;
	context->recur = 0;
}

void NONNULL(1)
mm_trace_context_cleanup(struct mm_trace_context *context)
{
	mm_arena_free(&mm_global_arena, context->owner);
}

#endif

/**********************************************************************
 * Trace level.
 **********************************************************************/

#if ENABLE_TRACE

static bool
mm_trace_enter(struct mm_trace_context *context, int level)
{
	if (unlikely(context->recur))
		return false;

	if (level < 0)
		context->level += level;
	context->recur++;

	return true;
}

static void
mm_trace_leave(struct mm_trace_context *context, int level)
{
	if (level > 0)
		context->level += level;
	context->recur--;
}

#endif

/**********************************************************************
 * Trace utilities.
 **********************************************************************/

#if ENABLE_TRACE

static void
mm_trace_prefix(void)
{
	struct mm_trace_context *context = mm_trace_getcontext();
	mm_log_fmt("%s %*s", context->owner, context->level * 2, "");
}

void NONNULL(2, 3, 4) FORMAT(4, 5)
mm_trace(int level,
	 const char *restrict location,
	 const char *restrict function,
	 const char *restrict msg, ...)
{
	struct mm_trace_context *context = mm_trace_getcontext();
	if (!mm_trace_enter(context, level))
		return;

	mm_where(location, function);

	va_list va;
	va_start(va, msg);
	mm_log_vfmt(msg, va);
	va_end(va);

	mm_log_str("\n");

	mm_trace_leave(context, level);
}

#endif
