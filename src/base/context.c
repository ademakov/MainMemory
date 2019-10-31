/*
 * base/context.c - MainMemory per-thread execution context.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#include "base/context.h"

#include "base/bitops.h"
#include "base/logger.h"
#include "base/runtime.h"
#include "base/event/listener.h"

#define MM_ASYNC_QUEUE_MIN_SIZE		(16)

// A context associated with the running thread.
__thread struct mm_context *__mm_context_self;

void NONNULL(1)
mm_context_prepare(struct mm_context *context, mm_thread_t ident, uint32_t async_queue_size)
{
	// Gather pointers to main runtime components.
	context->strand = mm_thread_ident_to_strand(ident);
	context->listener = mm_thread_ident_to_event_listener(ident);
	context->listener->context = context;

	// Prepare the event clock.
	mm_timesource_prepare(&context->timesource);

	// Prepare storage for tasks.
	mm_task_list_prepare(&context->tasks);

	// Create the async call queue.
	uint32_t sz = mm_upper_pow2(async_queue_size);
	if (sz < MM_ASYNC_QUEUE_MIN_SIZE)
		sz = MM_ASYNC_QUEUE_MIN_SIZE;
	context->async_queue = mm_ring_mpmc_create(sz);
}

void NONNULL(1)
mm_context_cleanup(struct mm_context *context UNUSED)
{
	// Destroy the associated async call queue.
	mm_ring_mpmc_destroy(context->async_queue);

	// Destroy storage for tasks.
	mm_task_list_cleanup(&context->tasks);

	context->listener->context = NULL;
}

/**********************************************************************
 * Asynchronous task scheduling.
 **********************************************************************/

void NONNULL(1, 2)
mm_context_add_task(struct mm_context *self, mm_task_t task, mm_value_t arg)
{
	ENTER();
	ASSERT(self == mm_context_selfptr());

	mm_task_list_add(&self->tasks, task, arg);

	LEAVE();
}

#if ENABLE_SMP

static void
mm_context_add_task_req(struct mm_context *context, uintptr_t *arguments)
{
	ENTER();

	struct mm_task *task = (struct mm_task *) arguments[0];
	mm_value_t arg = arguments[1];

	mm_context_add_task(context, task, arg);

	LEAVE();
}

#endif

void NONNULL(1, 2)
mm_context_send_task(struct mm_context *context, mm_task_t task, mm_value_t arg)
{
	ENTER();

#if ENABLE_SMP
	if (context == mm_context_selfptr()) {
		// Enqueue it directly if on the same strand.
		mm_context_add_task(context, task, arg);
	} else {
		// Submit the work item to the thread request queue.
		mm_event_call_2(context, mm_context_add_task_req, (uintptr_t) task, arg);
	}
#else
	mm_context_add_task(context, task, arg);
#endif

	LEAVE();
}

void NONNULL(1)
mm_context_post_task(mm_task_t task, mm_value_t arg)
{
	ENTER();

#if ENABLE_SMP
	// Dispatch the task.
	mm_event_post_2(mm_context_add_task_req, (mm_value_t) task, arg);
#else
	mm_context_add_task(mm_context_selfptr(), task, arg);
#endif

	LEAVE();
}

#if ENABLE_SMP
void NONNULL(1)
mm_context_distribute_tasks(struct mm_context *const self)
{
	ENTER();

	const size_t ntasks = mm_task_list_size(&self->tasks);
	if (ntasks > (10 * MM_TASK_SEND_MAX)) {
		static const uint32_t limit = 2 * MM_TASK_SEND_MAX;
		const mm_thread_t ncontexts = mm_number_of_regular_threads();

		uint32_t count = 0;
		for (uint32_t index = 0; index < ncontexts && count < 2; index++) {
			struct mm_context *const peer = mm_thread_ident_to_context(index);
			if (peer == self)
				continue;

			uint64_t n = mm_task_peer_list_size(&peer->tasks);
			n += mm_ring_mpmc_size(peer->async_queue) * MM_TASK_SEND_MAX;
			while (n < limit && mm_task_list_reassign(&self->tasks, peer))
				n += MM_TASK_SEND_MAX;
			count++;
		}
	}

	LEAVE();
}
#endif
