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

/*
 * MainMemory rings are multiple-producer/single-consumer fixed-size buffers
 * of pointers that are used to pass certain kinds of data to a target core.
 * 
 * The algorithm is based on the single-producer/single-consumer algorithm
 * described in the following paper:
 * 
 * John Giacomoni, Tipp Moseley, Manish Vachharajani
 * FastForward for Efficient Pipeline Parallelism: A Cache-Optimized
 * Concurrent Lock-Free Queue.
 *
 * However currently only the basic algorithm is implemented, the suggested
 * enhancements like temporal slipping are not. Instead it is extended to
 * allow multiple producers that are synchronized with a spinlock.
 */

#define MM_RING(name, size)			\
	struct {				\
		struct mm_ring name;            \
		void *name##_store[size - 1];	\
	};

struct mm_ring
{
	/* Consumer data. */
	size_t head __align(MM_CACHELINE);

	/* Producer data. */
	size_t tail __align(MM_CACHELINE);
	union
	{
		mm_task_lock_t core;
		mm_thread_lock_t global;
	} tail_lock;

	/* Shared read-only data. */
	size_t mask __align(MM_CACHELINE);

	/* Shared communication buffer. */
	void *ring[1];

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
		mm_memory_store(ring->ring[head], NULL);
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

static inline bool
mm_ring_core_trylock(struct mm_ring *ring)
{
	return mm_task_trylock(&ring->tail_lock.core);
}

static inline void
mm_ring_core_lock(struct mm_ring *ring)
{
	mm_task_lock(&ring->tail_lock.core);
}

static inline void
mm_ring_core_unlock(struct mm_ring *ring)
{
	mm_task_unlock(&ring->tail_lock.core);
}

static inline bool
mm_ring_global_trylock(struct mm_ring *ring)
{
	return mm_thread_trylock(&ring->tail_lock.global);
}

static inline void
mm_ring_global_lock(struct mm_ring *ring)
{
	mm_thread_lock(&ring->tail_lock.global);
}

static inline void
mm_ring_global_unlock(struct mm_ring *ring)
{
	mm_thread_unlock(&ring->tail_lock.global);
}

/* Multi-producer enqueue operation with synchronization for core threads. */
static inline bool
mm_ring_core_put(struct mm_ring *ring, void *new_data)
{
	if (mm_ring_core_trylock(ring)) {
		if (mm_ring_put(ring, new_data)) {
			mm_ring_core_unlock(ring);
			return true;
		}
		mm_ring_core_unlock(ring);
	}
	return false;
}

/* Multi-producer enqueue operation with synchronization for core and auxiliary
   threads. */
static inline bool
mm_ring_global_put(struct mm_ring *ring, void *new_data)
{
	if (mm_ring_global_trylock(ring)) {
		if (mm_ring_put(ring, new_data)) {
			mm_ring_global_unlock(ring);
			return true;
		}
		mm_ring_global_unlock(ring);
	}
	return false;
}

#endif /* RING_H */
