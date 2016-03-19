/*
 * core/combiner.c - MainMemory task combining synchronization.
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

#include "core/combiner.h"
#include "core/core.h"
#include "core/task.h"
#include "base/bitops.h"
#include "base/report.h"
#include "base/log/trace.h"
#include "base/memory/space.h"
#include "base/thread/domain.h"

struct mm_task_combiner *
mm_task_combiner_create(const char *name, size_t size, size_t handoff)
{
	ENTER();
	ASSERT(size != 0);

	// Round the ring size to a power of 2.
	size = mm_upper_pow2(size);

	// Find the required combiner size in bytes.
	size_t nbytes = sizeof(struct mm_task_combiner);
	nbytes += size * sizeof(struct mm_ring_node);

	// Create the combiner.
	struct mm_task_combiner *combiner = mm_common_aligned_alloc(MM_CACHELINE, nbytes);
	mm_task_combiner_prepare(combiner, name, size, handoff);

	LEAVE();
	return combiner;
}

void NONNULL(1)
mm_task_combiner_destroy(struct mm_task_combiner *combiner)
{
	ENTER();

	mm_common_free(combiner);

	LEAVE();
}

void NONNULL(1)
mm_task_combiner_prepare(struct mm_task_combiner *combiner, const char *name,
			 size_t size, size_t handoff)
{
	ENTER();

	mm_combiner_prepare(&combiner->combiner, size, handoff);

	MM_THREAD_LOCAL_ALLOC(mm_domain_selfptr(), name, combiner->wait_queue);
	for (mm_core_t core = 0; core < mm_core_getnum(); core++) {
		struct mm_list *wait_queue = MM_THREAD_LOCAL_DEREF(core, combiner->wait_queue);
		mm_list_prepare(wait_queue);
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_task_combiner_execute(struct mm_task_combiner *combiner,
			 mm_combiner_routine_t routine, uintptr_t data)
{
	ENTER();

	// Disable cancellation as the enqueue algorithm cannot be
	// safely undone if interrupted in the middle.
	int cancelstate;
	mm_task_setcancelstate(MM_TASK_CANCEL_DISABLE, &cancelstate);

	// Get per-core queue of pending requests.
	mm_core_t core = mm_core_self();
	struct mm_list *wait_queue = MM_THREAD_LOCAL_DEREF(core, combiner->wait_queue);

	// Add the current request to the per-core queue.
	struct mm_task *task = mm_task_selfptr();
	task->flags |= MM_TASK_COMBINING;
	mm_list_append(wait_queue, &task->wait_queue);

	// Wait until the current request becomes the head of the
	// per-core queue.
	while (mm_list_head(wait_queue) != &task->wait_queue)
		mm_task_block();

	mm_combiner_execute(&combiner->combiner, routine, data);

	// Remove the request from the per-core queue.
	mm_list_delete(&task->wait_queue);
	task->flags &= ~MM_TASK_COMBINING;

	// If the per-core queue is not empty then let its new head take
	// the next turn.
	if (!mm_list_empty(wait_queue)) {
		struct mm_link *link = mm_list_head(wait_queue);
		task = containerof(link, struct mm_task, wait_queue);
		mm_task_run(task);
	}

	// Restore cancellation.
	mm_task_setcancelstate(cancelstate, NULL);

	LEAVE();
}
