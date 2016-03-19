/*
 * base/util/libcall.c - MainMemory libc call warning.
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

#include "base/util/libcall.h"

#include "base/report.h"

void
mm_libcall(const char *name)
{
	static __thread int recursion_guard = 0;
	if (!recursion_guard) {
		++recursion_guard;
		mm_warning(0,
			"attempt to call a standard library function "
			"overridden by MainMemory: '%s'", name);
		--recursion_guard;
	}
}
