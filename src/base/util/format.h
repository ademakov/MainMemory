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

#ifndef BASE_UTIL_FORMAT_H
#define	BASE_UTIL_FORMAT_H

#include "common.h"
#include "base/mem/arena.h"
#include <stdarg.h>

char * mm_format(mm_arena_t arena, const char *restrict fmt, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(1, 2)));

char * mm_vformat(mm_arena_t arena, const char *restrict fmt, va_list va)
	__attribute__((nonnull(1, 2)));

#endif /* BASE_UTIL_FORMAT_H */
