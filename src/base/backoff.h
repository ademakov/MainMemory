/*
 * base/backoff.h - MainMemory contention back off.
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

#ifndef BASE_BACKOFF_H
#define BASE_BACKOFF_H

#include "common.h"
#include "arch/spin.h"

#define MM_BACKOFF_SMALL	(0xff)

typedef bool (*mm_backoff_yield_t)(void);

void mm_backoff_set_yield(mm_backoff_yield_t yield);

uint32_t mm_backoff_slow(uint32_t count);

static inline void
mm_backoff_fixed(uint32_t count)
{
	while (count--)
		mm_spin_pause();
}

static inline uint32_t
mm_backoff(uint32_t count)
{
	if (count < MM_BACKOFF_SMALL) {
		mm_backoff_fixed(count);
		return count + count + 1;
	} else {
		return mm_backoff_slow(count);
	}
}

#endif /* BASE_BACKOFF_H */
