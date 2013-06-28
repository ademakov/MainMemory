/*
 * ring.h - MainMemory single-consumer circular buffer of pointers.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#ifndef RING_H
#define RING_H

#include "common.h"
#include "lock.h"

struct mm_ring
{
	/* Consumer data. */
	size_t head __align(MM_CACHELINE);

	/* Producer data. */
	size_t tail __align(MM_CACHELINE);
	union
	{
		mm_core_lock_t core;
		mm_global_lock_t global;
	} tail_lock;

	/* Shared read-only data. */
	size_t mask __align(MM_CACHELINE);

	/* Shared communication buffer. */
	void *ring[];

} __align(MM_CACHELINE);

void mm_ring_prepare(struct mm_ring *ring, size_t size);

/* Single-consumer dequeue operation. */
static inline void *
mm_ring_get(struct mm_ring *ring)
{
	size_t head = ring->head;
	void *data = mm_memory_load(ring->ring[head]);
	if (data != NULL)
	{
		mm_memory_store(ring->ring[head], 0);
		ring->head = ((head + 1) & ring->mask);
	}
	return data;
}

/* Single-producer enqueue operation. */
static inline bool
mm_ring_put(struct mm_ring *ring, void *new_data)
{
	size_t tail = ring->tail;
	void *old_data = mm_memory_load(ring->ring[tail]);
	if (old_data == NULL)
	{
		mm_memory_store(ring->ring[tail], new_data);
		ring->tail = ((tail + 1) & ring->mask);
		return true;
	}
	return false;
}

/* Multi-producer enqueue operation with synchronization for core threads. */
static inline bool
mm_ring_core_put(struct mm_ring *ring, void *new_data)
{
	return (mm_core_trylock(&ring->tail_lock.core)
		&& mm_ring_put(ring, new_data));
}

/* Multi-producer enqueue operation with synchronization for core and auxiliary
   threads. */
static inline bool
mm_ring_global_put(struct mm_ring *ring, void *new_data)
{
	return (mm_global_trylock(&ring->tail_lock.global)
		&& mm_ring_put(ring, new_data));
}

#endif /* RING_H */
