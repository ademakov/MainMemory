/*
 * base/counter.h - MainMemory single-writer 64-bit monotonic counter.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#ifndef BASE_COUNTER_H
#define BASE_COUNTER_H

#include "common.h"
#include "base/atomic.h"

typedef struct mm_counter
{
	uint64_t ALIGN(sizeof(uint64_t)) value;
} mm_counter_t;

static inline void
mm_counter_prepare(mm_counter_t *counter, uint64_t value)
{
	counter->value = value;
}

static inline void
mm_counter_inc(mm_counter_t *counter)
{
	counter->value++;
}

static inline void
mm_counter_add(mm_counter_t *counter, uint64_t value)
{
	counter->value += value;
}

static inline uint64_t
mm_counter_load(mm_counter_t *counter)
{
#if MM_WORD_64BIT
	return mm_memory_load(counter->value);
#else
# if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define MM_HI 0
#  define MM_LO 1
# else
#  define MM_HI 1
#  define MM_LO 0
# endif
	uint32_t *pair = (uint32_t *) &counter->value;
	uint32_t hi = mm_memory_load(pair[MM_HI]);
	for (;;) {
		uint32_t lo = mm_memory_load(pair[MM_LO]);
		mm_memory_load_fence();
		uint32_t hi2 = mm_memory_load(pair[MM_HI]);
		if (hi == hi2)
			return ((uint64_t) hi << 32) | lo;
		hi = hi2;
	}
# undef MM_HI
# undef MM_LO
#endif
}

#endif /* BASE_COUNTER_H */
