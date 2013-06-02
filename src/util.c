/*
 * util.c - MainMemory utilities.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

/**********************************************************************
 * Memory Allocation Routines.
 **********************************************************************/

#include "alloc.h"

char *
mm_strdup(const char *s)
{
	size_t len = strlen(s) + 1;
	return memcpy(mm_alloc(len), s, len);
}

char *
mm_asprintf(const char *restrict fmt, ...)
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

	char *ptr = mm_alloc(++len);

	va_start(va, fmt);
	vsnprintf(ptr, len, fmt, va);
	va_end(va);

	return ptr;
}
