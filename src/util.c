/*
 * util.c - MainMemory utilities.
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

#include "util.h"

#include "log.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>

void
mm_set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		mm_fatal(errno, "fcntl(..., F_GETFL, ...)");

	flags |= O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags) < 0)
		mm_fatal(errno, "fcntl(..., F_SETFL, ...)");
}

void
mm_libc_call(const char *name)
{
	static __thread int recursion_guard = 0;
	if (!recursion_guard) {
		++recursion_guard;
		mm_warning(0, "attempt to call a libc function '%s'", name);
		--recursion_guard;
	}
}

/**********************************************************************
 * Memory Allocation Routines.
 **********************************************************************/

#include "alloc.h"

char *
mm_asprintf(const struct mm_allocator *alloc, const char *restrict fmt, ...)
{
	int len;
	va_list va;
	char dummy[1];

	va_start(va, fmt);
	len = vsnprintf(dummy, sizeof dummy, fmt, va);
	va_end(va);

	if (unlikely(len < 0)) {
		mm_fatal(errno, "invalid format string");
	}

	char *ptr = alloc->alloc(++len);

	va_start(va, fmt);
	vsnprintf(ptr, len, fmt, va);
	va_end(va);

	return ptr;
}
