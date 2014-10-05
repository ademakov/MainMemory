/*
 * ring.h - MainMemory single-consumer circular buffer of pointers.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#include "ring.h"
#include "trace.h"

void
mm_ring_prepare(struct mm_ring *ring, size_t size)
{
	size_t mask = size - 1;
	// The size must be a power of 2.
	ASSERT(size && (size & mask) == 0);

	ring->head = 0;
	ring->tail = 0;
	ring->mask = mask;

	for (size_t i = 0; i < size; i++) {
		ring->ring[i] = NULL;
	}
}

void
mm_ring_prepare_synch(struct mm_ring *ring, size_t size, uint8_t flags)
{
	mm_ring_prepare(ring, size);

	if ((flags & MM_RING_SHARED_GET) != 0)
		ring->head_lock.shared = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	else if ((flags & MM_RING_GLOBAL_GET) != 0)
		ring->head_lock.global = (mm_thread_lock_t) MM_THREAD_LOCK_INIT;

	if ((flags & MM_RING_SHARED_PUT) != 0)
		ring->tail_lock.shared = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	else if ((flags & MM_RING_GLOBAL_PUT) != 0)
		ring->tail_lock.global = (mm_thread_lock_t) MM_THREAD_LOCK_INIT;
}
