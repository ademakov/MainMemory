/*
 * base/util/format.h - MainMemory string format utility.
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

#include "base/util/format.h"
#include "base/log/error.h"

#include <stdio.h>
#include <stdarg.h>

char *
mm_format(const struct mm_arena *arena, const char *restrict fmt, ...)
{
	int len;
	va_list va;
	char dummy[1];

	va_start(va, fmt);
	len = vsnprintf(dummy, sizeof dummy, fmt, va);
	va_end(va);

	if (unlikely(len < 0))
		mm_fatal(errno, "invalid format string");

	char *ptr = mm_arena_alloc(arena, ++len);

	va_start(va, fmt);
	vsnprintf(ptr, len, fmt, va);
	va_end(va);

	return ptr;
}
