/*
 * base/report.c - MainMemory message logging.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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
#include "base/log/log.h"
#include "base/log/trace.h"

#include <stdarg.h>

static bool mm_verbose_enabled = false;
static bool mm_warning_enabled = false;

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
