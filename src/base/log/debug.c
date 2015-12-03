/*
 * base/log/debug.c - MainMemory debug utilities.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#include "base/log/debug.h"
#include "base/log/log.h"
#include "base/log/trace.h"
#include "base/util/exit.h"

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
