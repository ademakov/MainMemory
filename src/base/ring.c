/*
 * base/ring.h - MainMemory single-consumer circular buffer of pointers.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#include "base/ring.h"
#include "base/bitops.h"
#include "base/log/debug.h"
#include "base/mem/alloc.h"

/**********************************************************************
 * Common Ring Buffer Header.
 **********************************************************************/

static void
mm_ring_base_prepare(struct mm_ring_base *ring, size_t size)
{
	// The size must be a power of 2.
	ASSERT(mm_is_pow2(size));

	ring->head = 0;
	ring->tail = 0;
	ring->mask = size - 1;
}

void __attribute__((nonnull(1)))
mm_ring_base_prepare_locks(struct mm_ring_base *ring, uint8_t locks)
{
	if ((locks & MM_RING_LOCKED_GET) != 0)
		ring->head_lock = (mm_common_lock_t) MM_COMMON_LOCK_INIT;

	if ((locks & MM_RING_LOCKED_PUT) != 0)
		ring->tail_lock = (mm_common_lock_t) MM_COMMON_LOCK_INIT;
}

/**********************************************************************
 * Single-Producer Single-Consumer Ring Buffer.
 **********************************************************************/

struct mm_ring_spsc *
mm_ring_spsc_create(size_t size, uint8_t locks)
{
	// Find the required ring size in bytes.
	size_t nbytes = sizeof(struct mm_ring_spsc);
	nbytes += size * sizeof(void *);

	// Create the ring.
	struct mm_ring_spsc *ring = mm_global_aligned_alloc(MM_CACHELINE, nbytes);
	mm_ring_spsc_prepare(ring, size, locks);

	return ring;
}

void __attribute__((nonnull(1)))
mm_ring_spsc_destroy(struct mm_ring_spsc *ring)
{
	mm_global_free(ring);
}

void __attribute__((nonnull(1)))
mm_ring_spsc_prepare(struct mm_ring_spsc *ring, size_t size, uint8_t locks)
{
	mm_ring_base_prepare(&ring->base, size);
	mm_ring_base_prepare_locks(&ring->base, locks);

	for (size_t i = 0; i < size; i++) {
		ring->ring[i] = NULL;
	}
}

/**********************************************************************
 * Non-Blocking Multiple Producer Multiple Consumer Ring Buffer.
 **********************************************************************/

struct mm_ring_mpmc *
mm_ring_mpmc_create(size_t size)
{
	// Find the required ring size in bytes.
	size_t nbytes = sizeof(struct mm_ring_mpmc);
	nbytes += size * sizeof(struct mm_ring_node);

	// Create the ring.
	struct mm_ring_mpmc *ring = mm_global_aligned_alloc(MM_CACHELINE, nbytes);
	mm_ring_mpmc_prepare(ring, size);

	return ring;
}

void __attribute__((nonnull(1)))
mm_ring_mpmc_destroy(struct mm_ring_mpmc *ring)
{
	mm_global_free(ring);
}

void __attribute__((nonnull(1)))
mm_ring_mpmc_prepare(struct mm_ring_mpmc *ring, size_t size)
{
	mm_ring_base_prepare(&ring->base, size);

	for (size_t i = 0; i < size; i++) {
		ring->ring[i].lock = i;
	}
}
