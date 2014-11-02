/*
 * base/combiner.c - MainMemory synchronization via combining/delegation.
 *
 * Copyright (C) 2014  Aleksey Demakov
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
#include "base/log/trace.h"
#include "base/mem/alloc.h"
#include "base/thr/domain.h"

#include "core/core.h"
#include "core/task.h"

#define MM_COMBINER_MINIMUM_HANDOFF	4
#define MM_COMBINER_DEFAULT_HANDOFF	16

struct mm_combiner_wait_node
{
	struct mm_link link;
	struct mm_task *task;
};

struct mm_combiner *
mm_combiner_create(const char *name,
		   mm_combiner_routine_t routine,
		   size_t size, size_t handoff)
{
	ENTER();
	ASSERT(size != 0);

	// Round the ring size to a power of 2.
	size = 1 << (8 * sizeof(size_t) - mm_clz(size - 1));

	// Find the required combiner size in bytes.
	size_t nbytes = sizeof(struct mm_combiner);
	nbytes += size * sizeof(struct mm_ring_node);

	// Create the combiner.
	struct mm_combiner *combiner = mm_global_aligned_alloc(MM_CACHELINE, nbytes);
	mm_combiner_prepare(combiner, name, routine, size, handoff);

	LEAVE();
	return combiner;
}

void
mm_combiner_destroy(struct mm_combiner *combiner)
{
	mm_global_free(combiner);
}

void
mm_combiner_prepare(struct mm_combiner *combiner,
		    const char *name,
		    mm_combiner_routine_t routine,
		    size_t size, size_t handoff)
{
	ENTER();
	ASSERT(mm_is_pow2(size));

	if (handoff == 0)
		handoff = MM_COMBINER_DEFAULT_HANDOFF;
	if (handoff < MM_COMBINER_MINIMUM_HANDOFF)
		handoff = MM_COMBINER_MINIMUM_HANDOFF;

	combiner->routine = routine;
	combiner->handoff = handoff;

	MM_CDATA_ALLOC(mm_domain_self(), name, combiner->wait_list);
	for (mm_core_t core = 0; core < mm_core_getnum(); core++) {
		struct mm_queue *wait_list = MM_CDATA_DEREF(core, combiner->wait_list);
		mm_queue_init(wait_list);
	}

	mm_ring_mpmc_prepare(&combiner->ring, size);
	mm_ring_base_prepare_locks(&combiner->ring.base, MM_RING_LOCKED_GET);

	LEAVE();
}

bool
mm_combiner_combine(struct mm_combiner *combiner)
{
	ENTER();
	bool done = false;

	for (size_t i = 0; i < combiner->handoff; i++) {
		uintptr_t argument;
		if (mm_ring_relaxed_get(&combiner->ring, &argument)) {
			combiner->routine(argument);
		} else {
			done = true;
			break;
		}
	}

	LEAVE();
	return done;
}

static void
mm_combiner_busywait(struct mm_combiner *combiner,
		     struct mm_ring_node * node,
		     uintptr_t lock, bool more)
{
	ENTER();

	// Spin until the slot reaches the required state.
	uint32_t backoff = 0;
	while (mm_memory_load(node->lock) != lock) {
		if (unlikely(node->lock > lock)) {
			if (more)
				break;
			else
				ABORT();
		}

		// Pause for a small time.
		backoff = mm_backoff(backoff);

		// Try again to acquire the lock and execute pending requests.
		if (mm_combiner_trylock(combiner)) {
			mm_combiner_combine(combiner);
			mm_combiner_unlock(combiner);
			backoff = 0;
		}
	}

	LEAVE();
}

mm_value_t
mm_combiner_deplete(mm_value_t arg)
{
	ENTER();

	struct mm_combiner *combiner = (struct mm_combiner *) arg;

	while (mm_combiner_trylock(combiner)) {
		bool depleted = mm_combiner_combine(combiner);
		mm_combiner_unlock(combiner);
		if (depleted)
			break;
		mm_task_yield();
	}

	LEAVE();
	return 0;
}

void
mm_combiner_enqueue(struct mm_combiner *combiner, uintptr_t data, bool wait)
{
	ENTER();

	// Disable cancellation as the enqueue algorithm cannot be
	// safely undone if interrupted in the middle.
	int cancelstate;
	mm_task_setcancelstate(MM_TASK_CANCEL_DISABLE, &cancelstate);

	// Get per-core queue of pending requests.
	mm_core_t core = mm_core_selfid();
	struct mm_queue *wait_list = MM_CDATA_DEREF(core, combiner->wait_list);

	// Add the current request to the per-core queue.
	struct mm_combiner_wait_node wait_node;
	wait_node.task = mm_task_self();
	wait_node.task->flags |= MM_TASK_COMBINING;
	mm_queue_append(wait_list, &wait_node.link);

	// White until the current request becomes the head of the
	// per-core queue.
	while (mm_queue_head(wait_list) != &wait_node.link)
		mm_task_block();

	// Get a request slot in the bounded MPMC queue shared between cores.
	struct mm_ring_mpmc *ring = &combiner->ring;
	uintptr_t tail = mm_atomic_uintptr_fetch_and_add(&ring->base.tail, 1);
	struct mm_ring_node *node = &ring->ring[tail & ring->base.mask];

	// Wait until the slot becomes ready to accept the request.
	mm_combiner_busywait(combiner, node, tail, false);

	// Put the request to the slot.
	mm_memory_store(node->data, data);
	mm_memory_store_fence();
	mm_memory_store(node->lock, tail + 1);

	// Optionally wait until the request is seen.
	if (wait)
		mm_combiner_busywait(combiner, node, tail + 1 + ring->base.mask, true);

	// Remove the request from the per-core queue.
	mm_queue_delete_head(wait_list);
	wait_node.task->flags &= ~MM_TASK_COMBINING;

	// Restore cancellation.
	mm_task_setcancelstate(cancelstate, NULL);

	// If the per-core queue is not empty then let its new head take
	// the next turn.  Otherwise 
	if (!mm_queue_empty(wait_list)) {
		struct mm_link *link = mm_queue_head(wait_list);
		struct mm_combiner_wait_node *next =
			containerof(link, struct mm_combiner_wait_node, link);
		mm_task_run(next->task);
	} else if (!wait) {
		mm_core_post(MM_CORE_NONE, mm_combiner_deplete, (mm_value_t) combiner);
	}

	LEAVE();
}

void
mm_combiner_execute(struct mm_combiner *combiner, uintptr_t data, bool wait)
{
	ENTER();

	// Try to acquire the lock.  If successful execute the current request
	// along with a handful of queued requests.  Otherwise enqueue it to
	// be executed later.
	if (mm_combiner_trylock(combiner)) {
		combiner->routine(data);
		mm_combiner_combine(combiner);
		mm_combiner_unlock(combiner);
	} else {
		mm_combiner_enqueue(combiner, data, wait);
	}

	LEAVE();
}

