/*
 * base/thread/barrier.h - MainMemory barriers.
 *
 * Copyright (C) 2014-2015  Aleksey Demakov
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

#include "base/thread/barrier.h"

#include "base/atomic.h"
#include "base/thread/backoff.h"

void NONNULL(1)
mm_thread_barrier_prepare(struct mm_thread_barrier *barrier, uint32_t count)
{
	barrier->count = count;
	barrier->value = count;
	barrier->sense = 0;
}

void NONNULL(1)
mm_thread_barrier_local_prepare(struct mm_thread_barrier_local *local)
{
	local->sense = 0;
}

void NONNULL(1, 2)
mm_thread_barrier_wait(struct mm_thread_barrier *const barrier, struct mm_thread_barrier_local *local)
{
	uint32_t sense = ~local->sense;

	if (mm_atomic_uint32_dec_and_test(&barrier->value) == 0) {
		mm_memory_store(barrier->value, barrier->count);
		mm_memory_store_fence();
		mm_memory_store(barrier->sense, sense);
	} else {
		mm_memory_fence(); // TODO: atomic_load fence
		while (mm_memory_load(barrier->sense) != sense)
			mm_cpu_backoff();
	}

	local->sense = sense;
}
