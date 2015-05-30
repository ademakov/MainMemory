/*
 * base/backoff.c - MainMemory contention back off.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#include "base/backoff.h"
#include "base/thread/thread.h"

static mm_backoff_yield_t mm_backoff_yield;

void
mm_backoff_set_yield(mm_backoff_yield_t yield)
{
	mm_backoff_yield = yield;
}

uint32_t
mm_backoff_slow(uint32_t count)
{
	if (count > 0xffff) {
		mm_thread_yield();
		return 0;
	} else {
		if (mm_backoff_yield == NULL || !mm_backoff_yield())
			mm_backoff_fixed(count & 0xfff);
		return count + count + 1;
	}
}
