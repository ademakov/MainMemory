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

/**********************************************************************
 * Common Ring Buffer Header.
 **********************************************************************/

static void
mm_ring_base_prepare(struct mm_ring_base *ring, size_t size)
{
	size_t mask = size - 1;
	// The size must be a power of 2.
	ASSERT(size && (size & mask) == 0);

	ring->head = 0;
	ring->tail = 0;
	ring->mask = mask;
}

static void
mm_ring_base_prepare_locks(struct mm_ring_base *ring, uint8_t flags)
{
	if ((flags & MM_RING_SHARED_GET) != 0)
		ring->head_lock.shared = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	else if ((flags & MM_RING_GLOBAL_GET) != 0)
		ring->head_lock.global = (mm_thread_lock_t) MM_THREAD_LOCK_INIT;

	if ((flags & MM_RING_SHARED_PUT) != 0)
		ring->tail_lock.shared = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	else if ((flags & MM_RING_GLOBAL_PUT) != 0)
		ring->tail_lock.global = (mm_thread_lock_t) MM_THREAD_LOCK_INIT;
}

/**********************************************************************
 * Single-Producer Single-Consumer Ring Buffer.
 **********************************************************************/

void
mm_ring_spsc_prepare(struct mm_ring_spsc *ring, size_t size)
{
	mm_ring_base_prepare(&ring->base, size);

	for (size_t i = 0; i < size; i++) {
		ring->ring[i] = NULL;
	}
}

/**********************************************************************
 * Spinlock-Protected Multi-Producer Multi-Consumer Ring Buffer.
 **********************************************************************/

void
mm_ring_prepare_locked(struct mm_ring_spsc *ring, size_t size, uint8_t flags)
{
	mm_ring_spsc_prepare(ring, size);
	mm_ring_base_prepare_locks(&ring->base, flags);
}

/**********************************************************************
 * Non-Blocking Multiple Producer Multiple Consumer Ring Buffer.
 **********************************************************************/

void
mm_ring_mpmc_prepare(struct mm_ring_mpmc *ring, size_t size)
{
	mm_ring_base_prepare(&ring->base, size);

	for (size_t i = 0; i < size; i++) {
		ring->ring[i].lock = i;
	}
}
