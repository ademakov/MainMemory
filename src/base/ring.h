/*
 * base/ring.h - MainMemory single-consumer circular buffer of pointers.
 *
 * Copyright (C) 2013-2020  Aleksey Demakov
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
#include "thread/backoff.h"

/**********************************************************************
 * Non-Blocking Multiple Producer Multiple Consumer Ring Buffer.
 **********************************************************************/

/*
 * The algorithm is a variation of those described there:
 *
 * http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
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

#define MM_RING_MPMC_DATA_SIZE	(7u)

typedef mm_atomic_uint32_t mm_ring_atomic_t;

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

struct mm_ring_node
{
	mm_stamp_t lock;
	uintptr_t data[MM_RING_MPMC_DATA_SIZE];
};

struct mm_ring_mpmc
{
	/* Shared data. */
	struct mm_ring_node *ring CACHE_ALIGN;
	mm_stamp_t mask;
	uintptr_t data[6];

	/* Consumer data. */
	mm_ring_atomic_t head CACHE_ALIGN;

	/* Producer data. */
	mm_ring_atomic_t tail CACHE_ALIGN;
};

struct mm_ring_mpmc *
mm_ring_mpmc_create(size_t size);

void NONNULL(1)
mm_ring_mpmc_destroy(struct mm_ring_mpmc *ring);

void NONNULL(1)
mm_ring_mpmc_prepare(struct mm_ring_mpmc *ring, size_t size);

void NONNULL(1)
mm_ring_mpmc_cleanup(struct mm_ring_mpmc *ring);

static inline uint32_t
mm_ring_mpmc_size(struct mm_ring_mpmc *ring)
{
	uint32_t head = mm_memory_load(ring->head);
	mm_memory_load_fence();
	int32_t size = mm_memory_load(ring->tail) - head;
	return size > 0 ? size : 0;
}

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
	mm_stamp_t tail = mm_memory_load(ring->tail);
	struct mm_ring_node *node = &ring->ring[tail & ring->mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != tail)
		return false;
	if (mm_ring_atomic_cas(&ring->tail, tail, tail + 1) != tail)
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
	mm_stamp_t head = mm_memory_load(ring->head);
	struct mm_ring_node *node = &ring->ring[head & ring->mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != head + 1)
		return false;
	if (mm_ring_atomic_cas(&ring->head, head, head + 1) != head)
		return false;

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->mask);

	*stamp = head;
	return true;
}

/* Multi-Producer enqueue operation with busy wait. */
static inline void NONNULL(1, 2, 3)
mm_ring_mpmc_enqueue_sn(struct mm_ring_mpmc *ring, mm_stamp_t *restrict stamp,
			uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t tail = mm_ring_atomic_fai(&ring->tail);
	struct mm_ring_node *node = &ring->ring[tail & ring->mask];

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
	mm_stamp_t head = mm_ring_atomic_fai(&ring->head);
	struct mm_ring_node *node = &ring->ring[head & ring->mask];

	mm_ring_mpmc_busywait(node, head + 1);

	mm_memory_load_fence();

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->mask);

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
	return mm_memory_load(ring->tail);
}

static inline mm_stamp_t NONNULL(1)
mm_ring_mpmc_dequeue_stamp(struct mm_ring_mpmc *ring)
{
	return mm_memory_load(ring->head);
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
	mm_stamp_t tail = ring->tail;
	struct mm_ring_node *node = &ring->ring[tail & ring->mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != tail)
		return false;

	ring->tail = tail + 1;

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
	mm_stamp_t head = ring->head;
	struct mm_ring_node *node = &ring->ring[head & ring->mask];

	mm_memory_load_fence();
	if (mm_memory_load(node->lock) != head + 1)
		return false;

	ring->head = head + 1;

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->mask);

	return true;
}

/* Single-Producer enqueue operation for MPMC ring with busy wait. */
static inline void NONNULL(1, 2)
mm_ring_spmc_enqueue_n(struct mm_ring_mpmc *ring, uintptr_t *restrict data, const unsigned n)
{
	mm_stamp_t tail = ring->tail++;
	struct mm_ring_node *node = &ring->ring[tail & ring->mask];

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
	mm_stamp_t head = ring->head++;
	struct mm_ring_node *node = &ring->ring[head & ring->mask];

	mm_ring_mpmc_busywait(node, head + 1);

	uintptr_t *restrict node_data = node->data;
	for (unsigned i = 0; i < n; i++)
		data[i] = mm_memory_load(node_data[i]);

	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->lock, head + 1 + ring->mask);
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
	return ring->tail;
}

static inline mm_stamp_t NONNULL(1)
mm_ring_mpsc_dequeue_stamp(struct mm_ring_mpmc *ring)
{
	return ring->head;
}

#endif /* BASE_RING_H */
