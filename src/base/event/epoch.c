/*
 * base/event/epoch.c - MainMemory event sink reclamation epochs.
 *
 * Copyright (C) 2016-2017  Aleksey Demakov
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

#include "base/event/epoch.h"

#include "base/context.h"
#include "base/event/dispatch.h"
#include "base/event/listener.h"
#include "base/fiber/fiber.h"

#define MM_EVENT_EPOCH_POST_COUNT	(8)

static void
mm_event_epoch_observe_req(struct mm_event_listener *listener, uintptr_t *arguments UNUSED)
{
	ENTER();

	if (mm_event_epoch_active(&listener->epoch))
		mm_event_epoch_advance(&listener->epoch, &listener->dispatch->global_epoch);

	LEAVE();
}

static mm_value_t
mm_event_epoch_reclaim_execute(mm_value_t arg)
{
	ENTER();

	struct mm_event_fd *sink = (struct mm_event_fd *) arg;
	ASSERT(sink->listener == mm_context_listener());

	// Notify a reader/writer about closing.
	// TODO: don't block here, have a queue of closed sinks
	while (sink->input_fiber != NULL || sink->output_fiber != NULL) {
		struct mm_fiber *fiber = sink->listener->strand->fiber;
		mm_priority_t priority = MM_PRIO_UPPER(fiber->priority, 1);
		if (sink->input_fiber != NULL)
			mm_fiber_hoist(sink->input_fiber, priority);
		if (sink->output_fiber != NULL)
			mm_fiber_hoist(sink->output_fiber, priority);
		mm_fiber_yield();
	}

	// Destroy the sink.
	ASSERT(mm_event_closed(sink));
	(sink->destroy)(sink);

	LEAVE();
	return 0;
}

static void
mm_event_epoch_reclaim(struct mm_event_fd *sink)
{
	ENTER();

	// Upon this point there will be no any new I/O events related to
	// this sink. But there still may be active reader/writer fibers
	// or queued past work items for it. So relying on the FIFO order
	// of the work queue submit a work item that might safely cleanup
	// the socket being the last one that refers to it.
	MM_EVENT_TASK(reclaim_task,
		      mm_event_epoch_reclaim_execute,
		      mm_event_complete_noop,
		      mm_event_reassign_off);
	mm_event_add_task(sink->listener, &reclaim_task, (mm_value_t) sink);

	LEAVE();
}

void NONNULL(1)
mm_event_epoch_prepare(mm_event_epoch_t *global)
{
	*global = 1;
}

void NONNULL(1)
mm_event_epoch_prepare_local(struct mm_event_epoch_local *local)
{
	local->epoch = 0;
	local->count = 0;
	mm_queue_prepare(&local->queue);
	mm_stack_prepare(&local->limbo[0]);
	mm_stack_prepare(&local->limbo[1]);
}

void NONNULL(1, 2)
mm_event_epoch_advance(struct mm_event_epoch_local *local, mm_event_epoch_t *global)
{
	ENTER();

	// Update the local epoch snapshot. It might be seriously off here
	// because of using relaxed memory order in mm_event_epoch_enter().
	// However it might lag behind by more than one step only if both
	// limbo lists are empty. So there will be no any bad consequences.
	mm_event_epoch_snapshot_t epoch = mm_memory_load(*global);
	mm_memory_fence(); // TODO: load_store fence
	if (local->epoch != epoch) {
		// Reclaim event sinks from a past epoch if any.
		struct mm_stack *limbo = &local->limbo[(epoch >> 1) & 1];
		while (!mm_stack_empty(limbo)) {
			struct mm_slink *link = mm_stack_remove(limbo);
			struct mm_event_fd *sink = containerof(link, struct mm_event_fd, retire_link);
			mm_event_epoch_reclaim(sink);
			local->count--;
		}

		// Finish the critical section if all sinks are reclaimed.
		if (local->count == 0) {
			mm_memory_store(local->epoch, 0);
			goto leave;
		}

		// Remain in the critical section but amend the local epoch.
		mm_memory_store(local->epoch, epoch);
		local->index = 0;
	}

	// Put the retired sinks aside for future reclamation.
	if (!mm_queue_empty(&local->queue)) {
		struct mm_slink *head = (struct mm_slink *) mm_queue_head(&local->queue);
		struct mm_slink *tail = (struct mm_slink *) mm_queue_tail(&local->queue);
		struct mm_stack *limbo = &local->limbo[(epoch >> 1) & 1];
		mm_stack_insert_span(limbo, head, tail);
		mm_queue_prepare(&local->queue);
	}

	// Check to see if the global epoch can be advanced.
	struct mm_event_dispatch *dispatch = containerof(global, struct mm_event_dispatch, global_epoch);
	while (local->index < dispatch->nlisteners) {
		struct mm_event_listener *listener = &dispatch->listeners[local->index];
		mm_event_epoch_snapshot_t listener_epoch = mm_memory_load(listener->epoch.epoch);
		if (listener_epoch != epoch && listener_epoch != 0) {
			if (local->count > MM_EVENT_EPOCH_POST_COUNT)
				mm_event_call_0(listener, mm_event_epoch_observe_req);
			goto leave;
		}
		local->index++;
	}

	// Advance the global epoch.
	mm_atomic_uint32_cas(&dispatch->global_epoch, epoch, epoch + 2);
	DEBUG("advance epoch %u", epoch + 2);

leave:
	LEAVE();
}
