/*
 * base/combiner.c - MainMemory combining synchronization.
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

#include "base/combiner.h"

#include "base/bitops.h"
#include "base/report.h"
#include "base/memory/memory.h"

#define MM_COMBINER_MINIMUM_HANDOFF	4
#define MM_COMBINER_DEFAULT_HANDOFF	16

static size_t
mm_combiner_gethandoff(const struct mm_combiner *combiner)
{
	return combiner->ring.base.data[0];
}

static void
mm_combiner_sethandoff(struct mm_combiner *combiner, size_t value)
{
	combiner->ring.base.data[0] = value;
}

struct mm_combiner *
mm_combiner_create(size_t size, size_t handoff)
{
	ENTER();
	ASSERT(size != 0);

	// Round the ring size to a power of 2.
	size = mm_upper_pow2(size);

	// Find the required combiner size in bytes.
	size_t nbytes = sizeof(struct mm_combiner);
	nbytes += size * sizeof(struct mm_ring_node);

	// Create the combiner.
	struct mm_combiner *combiner = mm_common_aligned_alloc(MM_CACHELINE, nbytes);
	mm_combiner_prepare(combiner, size, handoff);

	LEAVE();
	return combiner;
}

void NONNULL(1)
mm_combiner_destroy(struct mm_combiner *combiner)
{
	ENTER();

	mm_common_free(combiner);

	LEAVE();
}

void NONNULL(1)
mm_combiner_prepare(struct mm_combiner *combiner, size_t size, size_t handoff)
{
	ENTER();
	ASSERT(mm_is_pow2(size));

	if (handoff == 0)
		handoff = MM_COMBINER_DEFAULT_HANDOFF;
	if (handoff < MM_COMBINER_MINIMUM_HANDOFF)
		handoff = MM_COMBINER_MINIMUM_HANDOFF;
	mm_combiner_sethandoff(combiner, handoff);

	mm_ring_mpmc_prepare(&combiner->ring, size);

	LEAVE();
}

void NONNULL(1, 2)
mm_combiner_execute(struct mm_combiner *combiner, mm_combiner_routine_t routine, uintptr_t data)
{
	ENTER();

	// Get a request slot in the bounded MPMC queue shared between cores.
	struct mm_ring_mpmc *ring = &combiner->ring;
	uintptr_t tail = mm_atomic_uintptr_fetch_and_add(&ring->base.tail, 1);
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	// Wait until the slot becomes ready to accept a request.
	uint32_t backoff = 0;
	while (mm_memory_load(node->lock) != tail)
		backoff = mm_thread_backoff(backoff);

	// Put the request to the slot.
	mm_memory_fence(); /* TODO: load_store fence */
	mm_memory_store(node->data[0], (uintptr_t) routine);
	mm_memory_store(node->data[1], data);
	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	// Wait until the request is executed.
	backoff = 0;
	do {
		// Check if it is actually our turn to execute the requests.
		uintptr_t head = mm_memory_load(ring->base.head);
		if (head == tail) {
			uintptr_t last = tail + mm_combiner_gethandoff(combiner);
			while (head != last) {
				struct mm_ring_node *node = &ring->ring[head & ring->base.mask];
				if (mm_memory_load(node->lock) != (head + 1))
					break;

				mm_memory_load_fence();
				uintptr_t data_0 = mm_memory_load(node->data[0]);
				uintptr_t data_1 = mm_memory_load(node->data[1]);
				mm_memory_fence(); /* TODO: load_store fence */
				mm_memory_store(node->lock, head + 1 + ring->base.mask);

				((mm_combiner_routine_t) data_0)(data_1);

				head = head + 1;
			}

			mm_memory_fence();
			mm_memory_store(ring->base.head, head);
			break;
		}

		backoff = mm_thread_backoff(backoff);

	} while (mm_memory_load(node->lock) == (tail + 1));

	LEAVE();
}
