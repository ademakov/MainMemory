/*
 * base/ring.h - MainMemory single-consumer circular buffer of pointers.
 *
 * Copyright (C) 2013-2016  Aleksey Demakov
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

#ifndef BASE_RING_H
#define BASE_RING_H

#include "common.h"
#include "base/atomic.h"
#include "base/lock.h"

/**********************************************************************
 * Common Ring Buffer Header.
 **********************************************************************/

#define MM_RING_LOCKED_PUT	1
#define MM_RING_LOCKED_GET	2

typedef mm_atomic_uint32_t mm_ring_atomic_t;

struct mm_ring_base
{
	/* Consumer data. */
	mm_ring_atomic_t head CACHE_ALIGN;
	mm_common_lock_t head_lock;

	/* Producer data. */
	mm_ring_atomic_t tail CACHE_ALIGN;
	mm_common_lock_t tail_lock;

	/* Shared data. */
	mm_stamp_t mask CACHE_ALIGN;
	uintptr_t data[7]; /* User data. */
};

void NONNULL(1)
mm_ring_base_prepare_locks(struct mm_ring_base *ring, uint8_t locks);

/* Multi-producer task synchronization. */
static inline bool
mm_ring_producer_locked(struct mm_ring_base *ring)
{
	return mm_common_is_locked(&ring->tail_lock);
}
static inline bool
mm_ring_producer_trylock(struct mm_ring_base *ring)
{
	return mm_common_trylock(&ring->tail_lock);
}
static inline void
mm_ring_producer_lock(struct mm_ring_base *ring)
{
	mm_common_lock(&ring->tail_lock);
}
static inline void
mm_ring_producer_unlock(struct mm_ring_base *ring)
{
	mm_common_unlock(&ring->tail_lock);
}

/* Multi-consumer task synchronization. */
static inline bool
mm_ring_consumer_locked(struct mm_ring_base *ring)
{
	return mm_common_is_locked(&ring->head_lock);
}
static inline bool
mm_ring_consumer_trylock(struct mm_ring_base *ring)
{
	return mm_common_trylock(&ring->head_lock);
}
static inline void
mm_ring_consumer_lock(struct mm_ring_base *ring)
{
	mm_common_lock(&ring->head_lock);
}
static inline void
mm_ring_consumer_unlock(struct mm_ring_base *ring)
{
	mm_common_unlock(&ring->head_lock);
}

static inline mm_stamp_t
mm_ring_atomic_fai(mm_ring_atomic_t *p)
{
	return mm_atomic_uint32_fetch_and_add(p, 1);
}

static inline mm_stamp_t
mm_ring_atomic_cas(mm_ring_atomic_t *p, mm_stamp_t e, mm_stamp_t v)
{
	return mm_atomic_uint32_cas(p, e, v);
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

struct mm_ring_spsc
{
	/* Ring header. */
	struct mm_ring_base base;
	/* Ring buffer. */
	void *ring[0];
};

struct mm_ring_spsc *
mm_ring_spsc_create(size_t size, uint8_t locks);

void NONNULL(1)
mm_ring_spsc_destroy(struct mm_ring_spsc *ring);

void NONNULL(1)
mm_ring_spsc_prepare(struct mm_ring_spsc *ring, size_t size, uint8_t locks);

/* Single-producer enqueue operation. */
static inline bool NONNULL(1, 2)
mm_ring_spsc_put(struct mm_ring_spsc *ring, void *data)
{
	mm_stamp_t tail = ring->base.tail;
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
static inline bool NONNULL(1, 2)
mm_ring_spsc_get(struct mm_ring_spsc *ring, void **data_ptr)
{
	mm_stamp_t head = ring->base.head;
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

/* Multi-producer enqueue operation with synchronization for tasks. */
static inline bool NONNULL(1, 2)
mm_ring_spsc_locked_put(struct mm_ring_spsc *ring, void *data)
{
	mm_ring_producer_lock(&ring->base);
	bool rc = mm_ring_spsc_put(ring, data);
	mm_ring_producer_unlock(&ring->base);
	return rc;
}

/* Multi-producer dequeue operation with synchronization for tasks. */
static inline bool NONNULL(1, 2)
mm_ring_spsc_locked_get(struct mm_ring_spsc *ring, void **data_ptr)
{
	mm_ring_consumer_lock(&ring->base);
	bool rc = mm_ring_spsc_get(ring, data_ptr);
	mm_ring_consumer_unlock(&ring->base);
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

#define MM_RING_MPMC_DATA_SIZE	(7)

struct mm_ring_node
{
	mm_stamp_t lock;
	uintptr_t data[MM_RING_MPMC_DATA_SIZE];
};

struct mm_ring_mpmc
{
	/* Ring header. */
	struct mm_ring_base base;
	/* Ring buffer. */
	struct mm_ring_node ring[0];
};

struct mm_ring_mpmc *
mm_ring_mpmc_create(size_t size);

void NONNULL(1)
mm_ring_mpmc_destroy(struct mm_ring_mpmc *ring);

void NONNULL(1)
mm_ring_mpmc_prepare(struct mm_ring_mpmc *ring, size_t size);

static inline void NONNULL(1)
mm_ring_mpmc_busywait(struct mm_ring_node *node, mm_stamp_t lock)
{
	uint32_t backoff = 0;
	while (mm_memory_load(node->lock) != lock)
		backoff = mm_thread_backoff(backoff);
}

/* Multi-Producer enqueue operation without wait. */
static inline bool NONNULL(1, 2, 3)
mm_ring_mpmc_put_sn(struct mm_ring_mpmc *ring, mm_stamp_t *restrict stamp,
		    uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t tail = mm_memory_load(ring->base.tail);
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != tail)
		return false;
	if (mm_ring_atomic_cas(&ring->base.tail, tail, tail + 1) != tail)
		return false;

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		mm_memory_store(node_data[i], data[i]);

	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	*stamp = tail;
	return true;
}

/* Multi-Consumer enqueue operation without wait. */
static inline bool NONNULL(1, 2, 3)
mm_ring_mpmc_get_sn(struct mm_ring_mpmc *ring, mm_stamp_t *restrict stamp,
		    uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t head = mm_memory_load(ring->base.head);
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != head + 1)
		return false;
	if (mm_ring_atomic_cas(&ring->base.head, head, head + 1) != head)
		return false;

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);

	*stamp = head;
	return true;
}

/* Multi-Producer enqueue operation with busy wait. */
static inline void NONNULL(1, 2, 3)
mm_ring_mpmc_enqueue_sn(struct mm_ring_mpmc *ring, mm_stamp_t *restrict stamp,
			uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t tail = mm_ring_atomic_fai(&ring->base.tail);
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_ring_mpmc_busywait(node, tail);

	mm_memory_fence(); /* TODO: load_store fence */

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		mm_memory_store(node_data[i], data[i]);

	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	*stamp = tail;
}

/* Multi-Consumer dequeue operation with busy wait. */
static inline void NONNULL(1, 2, 3)
mm_ring_mpmc_dequeue_sn(struct mm_ring_mpmc *ring, mm_stamp_t *restrict stamp,
			uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t head = mm_ring_atomic_fai(&ring->base.head);
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_ring_mpmc_busywait(node, head + 1);

	mm_memory_load_fence();

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);

	*stamp = head;
}

static inline bool NONNULL(1, 2)
mm_ring_mpmc_put_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t dummy;
	return mm_ring_mpmc_put_sn(ring, &dummy, data, n);
}

static inline bool NONNULL(1, 2)
mm_ring_mpmc_get_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t dummy;
	return mm_ring_mpmc_get_sn(ring, &dummy, data, n);
}

static inline void NONNULL(1, 2)
mm_ring_mpmc_enqueue_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t dummy;
	mm_ring_mpmc_enqueue_sn(ring, &dummy, data, n);
}

static inline void NONNULL(1, 2)
mm_ring_mpmc_dequeue_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t dummy;
	mm_ring_mpmc_dequeue_sn(ring, &dummy, data, n);
}

static inline bool NONNULL(1)
mm_ring_mpmc_put(struct mm_ring_mpmc *ring, uintptr_t data)
{
	return mm_ring_mpmc_put_n(ring, &data, 1);
}

static inline bool NONNULL(1, 2)
mm_ring_mpmc_get(struct mm_ring_mpmc *ring, uintptr_t *restrict data)
{
	return mm_ring_mpmc_get_n(ring, data, 1);
}

static inline void NONNULL(1)
mm_ring_mpmc_enqueue(struct mm_ring_mpmc *ring, uintptr_t data)
{
	mm_ring_mpmc_enqueue_n(ring, &data, 1);
}

static inline void NONNULL(1, 2)
mm_ring_mpmc_dequeue(struct mm_ring_mpmc *ring, uintptr_t *restrict data)
{
	mm_ring_mpmc_dequeue_n(ring, data, 1);
}

static inline mm_stamp_t NONNULL(1)
mm_ring_mpmc_enqueue_stamp(struct mm_ring_mpmc *ring)
{
	return mm_memory_load(ring->base.tail);
}

static inline mm_stamp_t NONNULL(1)
mm_ring_mpmc_dequeue_stamp(struct mm_ring_mpmc *ring)
{
	return mm_memory_load(ring->base.head);
}

/**********************************************************************
 * Relaxed Access to Multiple Producer Multiple Consumer Ring Buffer.
 **********************************************************************/

/* Relaxed access to MPMC ring buffer is to be used when it is known that
 * there is only one producer or consumer at the moment. */

/* Single-Producer enqueue operation for MPMC without wait. */
static inline bool NONNULL(1, 2)
mm_ring_spmc_put_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t tail = ring->base.tail;
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != tail)
		return false;

	ring->base.tail = tail + 1;

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		mm_memory_store(node_data[i], data[i]);

	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	return true;
}

/* Single-Consumer enqueue operation for MPMC without wait. */
static inline bool NONNULL(1, 2)
mm_ring_mpsc_get_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t head = ring->base.head;
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != head + 1)
		return false;

	ring->base.head = head + 1;

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);

	return true;
}

/* Single-Producer enqueue operation for MPMC ring with busy wait. */
static inline void NONNULL(1, 2)
mm_ring_spmc_enqueue_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t tail = ring->base.tail++;
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	mm_ring_mpmc_busywait(node, tail);

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		mm_memory_store(node_data[i], data[i]);

	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);
}

/* Single-Consumer dequeue operation for MPMC ring with busy wait. */
static inline void NONNULL(1, 2)
mm_ring_mpsc_dequeue_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t head = ring->base.head++;
	struct mm_ring_node *node = &ring->ring[head & ring->base.mask];

	mm_ring_mpmc_busywait(node, head + 1);

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->base.mask);
}

static inline bool NONNULL(1)
mm_ring_spmc_put(struct mm_ring_mpmc *ring, uintptr_t data)
{
	return mm_ring_spmc_put_n(ring, &data, 1);
}

static inline bool NONNULL(1, 2)
mm_ring_mpsc_get(struct mm_ring_mpmc *ring, uintptr_t *restrict data)
{
	return mm_ring_mpsc_get_n(ring, data, 1);
}

static inline void NONNULL(1)
mm_ring_spmc_enqueue(struct mm_ring_mpmc *ring, uintptr_t data)
{
	mm_ring_spmc_enqueue_n(ring, &data, 1);
}

static inline void NONNULL(1, 2)
mm_ring_mpsc_dequeue(struct mm_ring_mpmc *ring, uintptr_t *restrict data)
{
	mm_ring_mpsc_dequeue_n(ring, data, 1);
}

static inline mm_stamp_t NONNULL(1)
mm_ring_spmc_enqueue_stamp(struct mm_ring_mpmc *ring)
{
	return ring->base.tail;
}

static inline mm_stamp_t NONNULL(1)
mm_ring_mpsc_dequeue_stamp(struct mm_ring_mpmc *ring)
{
	return ring->base.head;
}

#endif /* BASE_RING_H */
