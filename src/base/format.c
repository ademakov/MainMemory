/*
 * base/format.c - MainMemory string format utility.
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

#include "base/format.h"

#include "base/report.h"

#include <stdio.h>

char * NONNULL(1, 2)
mm_vformat(mm_arena_t arena, const char *restrict fmt, va_list va)
{
	char dummy;
	va_list va2;
	va_copy(va2, va);
	int len = vsnprintf(&dummy, sizeof dummy, fmt, va2);
	va_end(va2);

	if (unlikely(len < 0))
		mm_fatal(errno, "invalid format string");

	char *ptr = mm_arena_alloc(arena, ++len);
	vsnprintf(ptr, len, fmt, va);

	return ptr;
}

char * NONNULL(1, 2) FORMAT(2, 3)
mm_format(const struct mm_arena *arena, const char *restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	char *ptr = mm_vformat(arena, fmt, va);
	va_end(va);
	return ptr;
}
