/*
 * util.h - MainMemory utilities.
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

#ifndef UTIL_H
#define UTIL_H

/* Forward declaration. */
struct mm_allocator;

void mm_set_nonblocking(int fd);

void mm_libc_call(const char *name);

char * mm_strdup(const struct mm_allocator *alloc, const char *s)
	__attribute__((nonnull(1)));

char * mm_asprintf(const struct mm_allocator *alloc, const char *restrict fmt, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(1, 2)));

#endif /* UTIL_H */
