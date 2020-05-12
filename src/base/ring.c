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

#include "base/ring.h"
#include "base/bitops.h"
#include "base/report.h"
#include "base/memory/alloc.h"

/**********************************************************************
 * Non-Blocking Multiple Producer Multiple Consumer Ring Buffer.
 **********************************************************************/

struct mm_ring_mpmc *
mm_ring_mpmc_create(size_t size)
{
	struct mm_ring_mpmc *ring = mm_memory_aligned_xalloc(MM_CACHELINE, sizeof(struct mm_ring_mpmc));
	mm_ring_mpmc_prepare(ring, size);
	return ring;
}

void NONNULL(1)
mm_ring_mpmc_destroy(struct mm_ring_mpmc *ring)
{
	mm_memory_free(ring);
}

void NONNULL(1)
mm_ring_mpmc_prepare(struct mm_ring_mpmc *ring, size_t size)
{
	// The size must be a power of 2.
	ASSERT(mm_is_pow2(size));

	ring->head = 0;
	ring->tail = 0;
	ring->mask = size - 1;

	size_t nbytes = size * sizeof(struct mm_ring_node);
	ring->ring = mm_memory_aligned_xalloc(MM_CACHELINE, nbytes);
	for (size_t i = 0; i < size; i++) {
		ring->ring[i].lock = i;
	}
}
