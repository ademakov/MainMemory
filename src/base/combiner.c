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
#include "base/memory/alloc.h"

#define MM_COMBINER_MINIMUM_HANDOFF	4
#define MM_COMBINER_DEFAULT_HANDOFF	16

static size_t
mm_combiner_gethandoff(const struct mm_combiner *combiner)
{
	return combiner->ring.data[0];
}

static void
mm_combiner_sethandoff(struct mm_combiner *combiner, size_t value)
{
	combiner->ring.data[0] = value;
}

struct mm_combiner *
mm_combiner_create(size_t size, size_t handoff)
{
	ENTER();
	ASSERT(size != 0);

	// Round the ring size to a power of 2.
	size = size > 4 ? mm_upper_pow2(size) : 4;

	// Find the required combiner size in bytes.
	size_t nbytes = sizeof(struct mm_combiner);
	nbytes += size * sizeof(struct mm_ring_node);

	// Create the combiner.
	struct mm_combiner *combiner = mm_memory_aligned_xalloc(MM_CACHELINE, nbytes);
	mm_combiner_prepare(combiner, size, handoff);

	LEAVE();
	return combiner;
}

void NONNULL(1)
mm_combiner_destroy(struct mm_combiner *combiner)
{
	ENTER();

	mm_memory_free(combiner);

	LEAVE();
}

void NONNULL(1)
mm_combiner_prepare(struct mm_combiner *combiner, size_t size, size_t handoff)
{
	ENTER();
	ASSERT(size >= 4 && mm_is_pow2(size));

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

	struct mm_ring_mpmc *const base = &combiner->ring;
	struct mm_ring_node *const ring = combiner->ring.ring;
	const mm_stamp_t mask = base->mask;

	// Get a request slot in the bounded MPMC queue shared between threads.
	const mm_stamp_t tail = mm_ring_atomic_fai(&base->tail);
	struct mm_ring_node *node = &ring[tail & mask];

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
	mm_stamp_t head = mm_memory_load(base->head);
	for (backoff = 0; head != tail; head = mm_memory_load(base->head)) {
		if (mm_memory_load(node->lock) != (tail + 1))
			goto leave;
#if 1
		mm_thread_backoff_fixed(backoff++ & 0x7);
#else
		backoff = mm_thread_backoff(backoff);
#endif
	}

	// It is actually our turn to execute requests.
	mm_stamp_t last = tail + mm_combiner_gethandoff(combiner);
#if 1
	for (; head != last; head++) {
		// Check if there is another pending request.
		struct mm_ring_node *node = &ring[head & mask];
		if (mm_memory_load(node->lock) != (head + 1))
			break;

		// Get the request.
		mm_memory_load_fence();
		uintptr_t data_0 = mm_memory_load(node->data[0]);
		uintptr_t data_1 = mm_memory_load(node->data[1]);
		mm_memory_fence(); /* TODO: load_store fence */
		mm_memory_store(node->lock, head + 1 + mask);

		// Execute the request.
		((mm_combiner_routine_t) data_0)(data_1);
	}
#else
	mm_stamp_t next = tail + 1;
	do {
		// Execute pending requests.
		for (; head != next; head++) {
			// Get the request.
			struct mm_ring_node *node = &ring[head & mask];
			uintptr_t data_0 = mm_memory_load(node->data[0]);
			uintptr_t data_1 = mm_memory_load(node->data[1]);
			mm_memory_fence(); /* TODO: load_store fence */
			mm_memory_store(node->lock, head + 1 + mask);

			// Execute the request.
			((mm_combiner_routine_t) data_0)(data_1);
		}

		// Check if there are more pending requests.
		for (; next != last; next++) {
			struct mm_ring_node *node = &ring[next & mask];
			if (mm_memory_load(node->lock) != (next + 1))
				break;

			mm_memory_fence();
			mm_memory_store(node->lock, next + 2);
		}

	} while (head != next);
#endif

	mm_memory_fence();
	mm_memory_store(base->head, head);

leave:
	LEAVE();
}
