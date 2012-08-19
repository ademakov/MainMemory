/*
 * util.c - MainMemory utilities.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#include <util.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if ENABLE_TRACE
static int mm_trace_level = 0;
#endif

static void
mm_vprint(const char *restrict msg, va_list va)
{
	vfprintf(stderr, msg, va);
}

void
mm_print(const char *restrict msg, ...)
{
	va_list va;
	va_start(va, msg);
	vfprintf(stderr, msg, va);
	va_end(va);
}

void
mm_flush(void)
{
	fflush(stderr);
}

void
mm_error(int error, const char *restrict msg, ...)
{
	va_list va;
	va_start(va, msg);
	mm_vprint(msg, va);
	va_end(va);

	if (error) {
		mm_print(": %s\n", strerror(error));
	} else {
		mm_print("\n");
	}
}

void
mm_fatal(int error, const char *restrict msg, ...)
{
	ENTER();

	va_list va;
	va_start(va, msg);
	mm_vprint(msg, va);
	va_end(va);

	if (error) {
		mm_print(": %s\n", strerror(error));
	} else {
		mm_print("\n");
	}
	mm_flush();

	exit(EXIT_FAILURE);
}

void
mm_abort(const char *file, int line, const char *func,
	 const char *restrict msg, ...)
{
	ENTER();

	mm_print("%s:%d, %s: ", file, line, func);

	va_list va;
	va_start(va, msg);
	mm_vprint(msg, va);
	va_end(va);

	mm_print("\n");
	mm_flush();

	abort();
}

#if ENABLE_TRACE

void
mm_trace_enter(void)
{
	++mm_trace_level;
}

void
mm_trace_leave(void)
{
	--mm_trace_level;
}

void
mm_trace(const char *file, int line, const char *func, const char *restrict msg, ...)
{
	mm_print("%*s%s ", mm_trace_level * 2, "", func);

	va_list va;
	va_start(va, msg);
	mm_vprint(msg, va);
	va_end(va);

	mm_print(" (%s:%d)\n", file, line);
}

#endif
