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

#ifndef RING_H
#define RING_H

#include "common.h"
#include "arch/atomic.h"
#include "lock.h"

/**********************************************************************
 * Common Ring Buffer Header.
 **********************************************************************/

#define MM_RING_SHARED_PUT	1
#define MM_RING_GLOBAL_PUT	2
#define MM_RING_SHARED_GET	4
#define MM_RING_GLOBAL_GET	8

struct mm_ring_base
{
	/* Consumer data. */
	mm_atomic_uintptr_t head __align(MM_CACHELINE);
	union
	{
		mm_task_lock_t shared;
		mm_thread_lock_t global;
	} head_lock;

	/* Producer data. */
	mm_atomic_uintptr_t tail __align(MM_CACHELINE);
	union
	{
		mm_task_lock_t shared;
		mm_thread_lock_t global;
	} tail_lock;

	/* Shared data. */
	uintptr_t mask __align(MM_CACHELINE);
};

/* Multi-producer task synchronization. */
static inline bool
mm_ring_sharedput_trylock(struct mm_ring_base *ring)
{
	return mm_task_trylock(&ring->tail_lock.shared);
}
static inline void
mm_ring_sharedput_lock(struct mm_ring_base *ring)
{
	mm_task_lock(&ring->tail_lock.shared);
}
static inline void
mm_ring_sharedput_unlock(struct mm_ring_base *ring)
{
	mm_task_unlock(&ring->tail_lock.shared);
}

/* Multi-consumer task synchronization. */
static inline bool
mm_ring_sharedget_trylock(struct mm_ring_base *ring)
{
	return mm_task_trylock(&ring->head_lock.shared);
}
static inline void
mm_ring_sharedget_lock(struct mm_ring_base *ring)
{
	mm_task_lock(&ring->head_lock.shared);
}
static inline void
mm_ring_sharedget_unlock(struct mm_ring_base *ring)
{
	mm_task_unlock(&ring->head_lock.shared);
}

/* Multi-producer thread synchronization. */
static inline bool
mm_ring_globalput_trylock(struct mm_ring_base *ring)
{
	return mm_thread_trylock(&ring->tail_lock.global);
}
static inline void
mm_ring_globalput_lock(struct mm_ring_base *ring)
{
	mm_thread_lock(&ring->tail_lock.global);
}
static inline void
mm_ring_globalput_unlock(struct mm_ring_base *ring)
{
	mm_thread_unlock(&ring->tail_lock.global);
}

/* Multi-consumer thread synchronization. */
static inline bool
mm_ring_globalget_trylock(struct mm_ring_base *ring)
{
	return mm_thread_trylock(&ring->head_lock.global);
}
static inline void
mm_ring_globalget_lock(struct mm_ring_base *ring)
{
	mm_thread_lock(&ring->head_lock.global);
}
static inline void
mm_ring_globalget_unlock(struct mm_ring_base *ring)
{
	mm_thread_unlock(&ring->head_lock.global);
}

/**********************************************************************
 * Single-Producer Single-Consumer Ring Buffer.
 **********************************************************************/

/*
 * The algorithm is based on the single-producer/single-consumer algorithm
 * described in the following paper:
 * 
 * John Giacomoni, Tipp Moseley, Manish Vachharajani
 * FastForward for Efficient Pipeline Parallelism: A Cache-Optimized
 * Concurrent Lock-Free Queue.
 *
 * However currently only the basic algorithm is implemented, the suggested
 * enhancements like temporal slipping are not.
 * 
 * Instead it is extended to optionally support multiple producers and
 * consumers with spinlock protection.
 */

#define MM_RING_SPSC(name, size)					\
	union {								\
		struct mm_ring_spsc name;					\
		struct {						\
			struct mm_ring_base base;			\
			void *ring[size];				\
		} name##_store;						\
	}

struct mm_ring_spsc
{
	/* Ring header. */
	struct mm_ring_base base;
	/* Ring buffer. */
	void *ring[0];
};

void mm_ring_spsc_prepare(struct mm_ring_spsc *ring, size_t size)
	__attribute__((nonnull(1)));

/* Single-producer enqueue operation. */
static inline bool
mm_ring_spsc_put(struct mm_ring_spsc *ring, void *data)
{
	uintptr_t tail = ring->base.tail;
	void *prev = mm_memory_load(ring->ring[tail]);
	if (prev == NULL)
	{
		mm_memory_store(ring->ring[tail], data);
		ring->base.tail = ((tail + 1) & ring->base.mask);
		return true;
	}
	return false;
}

/* Single-consumer dequeue operation. */
static inline bool
mm_ring_spsc_get(struct mm_ring_spsc *ring, void **data_ptr)
{
	uintptr_t head = ring->base.head;
	void *data = mm_memory_load(ring->ring[head]);
	if (data != NULL)
	{
		mm_memory_store(ring->ring[head], NULL);
		ring->base.head = ((head + 1) & ring->base.mask);
		*data_ptr = data;
		return true;
	}
	return false;
}

/**********************************************************************
 * Spinlock-Protected Multi-Producer Multi-Consumer Ring Buffer.
 **********************************************************************/

void mm_ring_prepare_locked(struct mm_ring_spsc *ring, size_t size, uint8_t flags)
	__attribute__((nonnull(1)));

/* Multi-producer enqueue operation with synchronization for tasks. */
static inline bool
mm_ring_shared_put(struct mm_ring_spsc *ring, void *data)
{
	mm_ring_sharedput_lock(&ring->base);
	bool rc = mm_ring_spsc_put(ring, data);
	mm_ring_sharedput_unlock(&ring->base);
	return rc;
}

/* Multi-producer dequeue operation with synchronization for tasks. */
static inline bool
mm_ring_shared_get(struct mm_ring_spsc *ring, void **data_ptr)
{
	mm_ring_sharedget_lock(&ring->base);
	bool rc = mm_ring_spsc_get(ring, data_ptr);
	mm_ring_sharedget_unlock(&ring->base);
	return rc;
}

/* Multi-producer enqueue operation with synchronization for threads. */
static inline bool
mm_ring_global_put(struct mm_ring_spsc *ring, void *data)
{
	mm_ring_globalput_lock(&ring->base);
	bool rc = mm_ring_spsc_put(ring, data);
	mm_ring_globalput_unlock(&ring->base);
	return rc;
}

/* Multi-producer dequeue operation with synchronization for threads. */
static inline bool
mm_ring_global_get(struct mm_ring_spsc *ring, void **data_ptr)
{
	mm_ring_globalget_lock(&ring->base);
	bool rc = mm_ring_spsc_get(ring, data_ptr);
	mm_ring_globalget_unlock(&ring->base);
	return rc;
}

/**********************************************************************
 * Non-Blocking Multiple Producer Multiple Consumer Ring Buffer.
 **********************************************************************/

/*
 * The algorithm is a variation of those described in the following papres:
 *
 * Massimiliano Meneghin, Davide Pasetto, Hubertus Franke, Fabrizio Petrini,
 * Jimi Xenidis
 * Performance evaluation of inter-thread communication mechanisms on
 * multicore/multithreaded architectures.
 *
 * Thomas R. W. Scogland, Wu-chun Feng
 * Design and Evaluation of Scalable Concurrent Queues for Many-Core
 * Architectures.
 */

#define MM_RING_MPMC(name, size)					\
	union {								\
		struct mm_ring_mpmc name;				\
		struct {						\
			struct mm_ring_base base;			\
			struct mm_ring_node name##_store[size - 1];	\
		} name##_store;						\
	}

struct mm_ring_node
{
	uintptr_t data;
	uintptr_t lock;
};

struct mm_ring_mpmc
{
	/* Ring header. */
	struct mm_ring_base base;
	/* Ring buffer. */
	struct mm_ring_node ring[0];
};

void mm_ring_mpmc_prepare(struct mm_ring_mpmc *ring, size_t size)
	__attribute__((nonnull(1)));

static inline void
mm_ring_mpmc_busywait(struct mm_ring_node *node, uintptr_t lock)
{
	uint32_t backoff = 0;
	while (mm_memory_load(node->lock) != lock)
		backoff = mm_backoff(backoff);
}

/* Multi-Producer enqueue operation without wait. */
static inline bool
mm_ring_mpmc_put(struct mm_ring_mpmc *ring, uintptr_t data)
{
	uintptr_t tail = mm_memory_load(ring->base.tail);
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != tail)
		return false;
	if (mm_atomic_uintptr_cas(&ring->base.tail, tail, tail + 1) != tail)
		return false;

	mm_memory_store(node->data, data);
	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	return true;
}

/* Multi-Consumer enqueue operation without wait. */
static inline bool
mm_ring_mpmc_get(struct mm_ring_mpmc *ring, uintptr_t *data_ptr)
{
	uintptr_t head = mm_memory_load(ring->base.head);
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != head + 1)
		return false;
	if (mm_atomic_uintptr_cas(&ring->base.head, head, head + 1) != head)
		return false;

	*data_ptr = mm_memory_load(node->data);
	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);

	return true;
}

/* Multi-Producer enqueue operation with busy wait. */
static inline bool
mm_ring_mpmc_enqueue(struct mm_ring_mpmc *ring, uintptr_t data)
{
	uintptr_t tail = mm_atomic_uintptr_fetch_and_add(&ring->base.tail, 1);
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_ring_mpmc_busywait(node, tail);

	mm_memory_store(node->data, data);
	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	return true;
}

/* Multi-Consumer dequeue operation with busy wait. */
static inline bool
mm_ring_mpmc_dequeue(struct mm_ring_mpmc *ring, uintptr_t *data_ptr)
{
	uintptr_t head = mm_atomic_uintptr_fetch_and_add(&ring->base.head, 1);
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_ring_mpmc_busywait(node, head + 1);

	*data_ptr = mm_memory_load(node->data);
	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);

	return true;
}

/**********************************************************************
 * Relaxed Access to Multiple Producer Multiple Consumer Ring Buffer.
 **********************************************************************/

/* Relaxed access to MPMC ring buffer is to be used when it is known that
 * there is only one producer or consumer at the moment. */

/* Single-Producer enqueue operation for MPMC without wait. */
static inline bool
mm_ring_relaxed_put(struct mm_ring_mpmc *ring, uintptr_t data)
{
	uintptr_t tail = ring->base.tail;
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != tail)
		return false;

	ring->base.tail = tail + 1;

	mm_memory_store(node->data, data);
	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	return true;
}

/* Single-Consumer enqueue operation for MPMC without wait. */
static inline bool
mm_ring_relaxed_get(struct mm_ring_mpmc *ring, uintptr_t *data_ptr)
{
	uintptr_t head = ring->base.head;
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != head + 1)
		return false;

	ring->base.head = head + 1;

	*data_ptr = mm_memory_load(node->data);
	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);

	return true;
}

/* Single-Producer enqueue operation for MPMC ring with busy wait. */
static inline bool
mm_ring_relaxed_enqueue(struct mm_ring_mpmc *ring, uintptr_t data)
{
	uintptr_t tail = ring->base.tail++;
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_ring_mpmc_busywait(node, tail);

	mm_memory_store(node->data, data);
	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	return true;
}

/* Single-Consumer dequeue operation for MPMC ring with busy wait. */
static inline bool
mm_ring_relaxed_dequeue(struct mm_ring_mpmc *ring, uintptr_t *data_ptr)
{
	uintptr_t head = ring->base.head++;
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_ring_mpmc_busywait(node, head + 1);

	*data_ptr = mm_memory_load(node->data);
	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);

	return true;
}

#endif /* RING_H */
