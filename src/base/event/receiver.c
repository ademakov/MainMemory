/*
 * base/event/receiver.c - MainMemory event receiver.
 *
 * Copyright (C) 2015-2016  Aleksey Demakov
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

#include "base/event/receiver.h"

#include "base/bitops.h"
#include "base/report.h"
#include "base/event/dispatch.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"

/**********************************************************************
 * Event forward request handlers.
 **********************************************************************/

static void
mm_event_receiver_forward_1(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[1];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);

	LEAVE();
}

static void
mm_event_receiver_forward_2(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[2];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], events >> 4);

	LEAVE();
}

static void
mm_event_receiver_forward_3(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[3];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[2], events >> 8);

	LEAVE();
}

static void
mm_event_receiver_forward_4(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[4];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[3], events >> 12);

	LEAVE();
}

static void
mm_event_receiver_forward_5(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	uintptr_t events = arguments[5];
	mm_event_convey((struct mm_event_fd *) arguments[0], events & 15);
	mm_event_convey((struct mm_event_fd *) arguments[1], (events >> 4) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[2], (events >> 8) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[3], (events >> 12) & 15);
	mm_event_convey((struct mm_event_fd *) arguments[4], events >> 16);

	LEAVE();
}

/**********************************************************************
 * Event forwarding.
 **********************************************************************/

static void
mm_event_receiver_fwdbuf_prepare(struct mm_event_receiver_fwdbuf *buffer)
{
	buffer->nsinks = 0;
}

static void
mm_event_receiver_forward_flush(struct mm_thread *thread, struct mm_event_receiver_fwdbuf *buffer)
{
	ENTER();

	switch (buffer->nsinks) {
	case 0:
		break;
	case 1:
		buffer->nsinks = 0;
		mm_thread_post_2(thread, mm_event_receiver_forward_1,
				 (uintptr_t) buffer->sinks[0],
				 buffer->events[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_thread_post_3(thread, mm_event_receiver_forward_2,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 buffer->events[0]
				 | (buffer->events[1] << 4));
		break;
	case 3:
		buffer->nsinks = 0;
		mm_thread_post_4(thread, mm_event_receiver_forward_3,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 buffer->events[0]
				 | (buffer->events[1] << 4)
				 | (buffer->events[2] << 8));
		break;
	case 4:
		buffer->nsinks = 0;
		mm_thread_post_5(thread, mm_event_receiver_forward_4,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3],
				 buffer->events[0]
				 | (buffer->events[1] << 4)
				 | (buffer->events[2] << 8)
				 | (buffer->events[3] << 12));
		break;
	case 5:
		buffer->nsinks = 0;
		mm_thread_post_6(thread, mm_event_receiver_forward_5,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3],
				 (uintptr_t) buffer->sinks[4],
				 buffer->events[0]
				 | (buffer->events[1] << 4)
				 | (buffer->events[2] << 8)
				 | (buffer->events[3] << 12)
				 | (buffer->events[4] << 16));
		break;
	default:
		ABORT();
	}

	LEAVE();
}

static void
mm_event_receiver_forward(struct mm_event_receiver *receiver, struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();

	mm_thread_t target = sink->target;
	struct mm_event_dispatch *dispatch = receiver->dispatch;
	struct mm_event_listener *listener = &dispatch->listeners[target];
	struct mm_event_receiver_fwdbuf *buffer = &receiver->forward_buffers[target];

	// Flush the buffer if it is full.
	if (buffer->nsinks == MM_EVENT_RECEIVER_FWDBUF_SIZE)
		mm_event_receiver_forward_flush(listener->thread, buffer);

	// Add the event to the buffer.
	unsigned int n = buffer->nsinks++;
	buffer->sinks[n] = sink;
	buffer->events[n] = event;

	// Account for it.
	mm_bitset_set(&receiver->forward_targets, target);
	receiver->forwarded_events++;

	LEAVE();
}

/**********************************************************************
 * Event sink queue.
 **********************************************************************/

static void
mm_event_receiver_enqueue(struct mm_event_receiver *receiver, struct mm_event_fd *sink, mm_event_t event)
{
	uint8_t bit = 1 << event;
	if (sink->queued_events == 0) {
		sink->queued_events = bit;
		receiver->enqueued_events++;

		struct mm_event_dispatch *dispatch = receiver->dispatch;
		uint32_t mask = dispatch->sink_queue_size - 1;
		uint32_t index = dispatch->sink_queue_tail++ & mask;
		dispatch->sink_queue[index] = sink;

	} else if ((sink->queued_events & bit) == 0) {
		sink->queued_events |= bit;
		receiver->enqueued_events++;
	}
}

static void
mm_event_receiver_restore(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	sink->target = receiver->thread;
	while (sink->queued_events) {
		mm_event_t event = mm_ctz(sink->queued_events);
		sink->queued_events ^= 1 << event;
		mm_event_convey(sink, event);
		receiver->dequeued_events++;
	}
}

static void
mm_event_receiver_dequeue(struct mm_event_receiver *receiver, struct mm_event_dispatch *dispatch)
{
	uint32_t mask = dispatch->sink_queue_size - 1;
	uint32_t index = dispatch->sink_queue_head++ & mask;
	struct mm_event_fd *sink = dispatch->sink_queue[index];
	mm_event_receiver_restore(receiver, sink);
}

/**********************************************************************
 * Event sink reclamation.
 **********************************************************************/

static bool
mm_event_receiver_reclaim_queue_empty(struct mm_event_receiver *receiver)
{
	return (mm_stack_empty(&receiver->reclaim_queue[0])
		&& mm_stack_empty(&receiver->reclaim_queue[1]));
}

static void
mm_event_receiver_reclaim_queue_insert(struct mm_event_receiver *receiver,
				       struct mm_event_fd *sink)
{
	uint32_t epoch = receiver->reclaim_epoch;
	struct mm_stack *stack = &receiver->reclaim_queue[epoch & 1];
	mm_stack_insert(stack, &sink->reclaim_link);
}

static void
mm_event_receiver_reclaim_epoch(struct mm_event_receiver *receiver, uint32_t epoch)
{
	struct mm_stack *stack = &receiver->reclaim_queue[epoch & 1];
	while (!mm_stack_empty(stack)) {
		struct mm_slink *link = mm_stack_remove(stack);
		struct mm_event_fd *sink = containerof(link, struct mm_event_fd, reclaim_link);
		mm_event_convey(sink, MM_EVENT_RECLAIM);
	}
}

void NONNULL(1)
mm_event_receiver_observe_epoch(struct mm_event_receiver *receiver)
{
	ENTER();

	// Reclaim queued event sinks associated with a past epoch.
	uint32_t epoch = mm_memory_load(receiver->dispatch->reclaim_epoch);
	uint32_t local = receiver->reclaim_epoch;
	if (local != epoch) {
		VERIFY((local + 1) == epoch);
		mm_memory_store(receiver->reclaim_epoch, epoch);
		mm_event_receiver_reclaim_epoch(receiver, epoch);
		mm_event_dispatch_advance_epoch(receiver->dispatch);
	}

	// Finish reclamation if there are no more queued event sinks.
	if (mm_event_receiver_reclaim_queue_empty(receiver)) {
		mm_memory_store_fence();
		mm_memory_store(receiver->reclaim_active, false);
	}

	LEAVE();
}

/**********************************************************************
 * Event receiver.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_receiver_prepare(struct mm_event_receiver *receiver, struct mm_event_dispatch *dispatch,
			  mm_thread_t thread)
{
	ENTER();

	// Initialize the reclamation data.
	receiver->reclaim_epoch = 0;
	receiver->reclaim_active = false;
	mm_stack_prepare(&receiver->reclaim_queue[0]);
	mm_stack_prepare(&receiver->reclaim_queue[1]);

	// Remember the owners.
	receiver->thread = thread;
	receiver->dispatch = dispatch;

	// Prepare forward buffers.
	receiver->forward_buffers = mm_common_calloc(dispatch->nlisteners,
						     sizeof(struct mm_event_receiver_fwdbuf));
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_event_receiver_fwdbuf_prepare(&receiver->forward_buffers[i]);
	mm_bitset_prepare(&receiver->forward_targets, &mm_common_space.xarena,
			  dispatch->nlisteners);

	// Initialize event statistics.
	receiver->stats.loose_events = 0;
	receiver->stats.direct_events = 0;
	receiver->stats.enqueued_events = 0;
	receiver->stats.dequeued_events = 0;
	receiver->stats.forwarded_events = 0;

	// Initialize private event storage.
	mm_event_backend_storage_prepare(&receiver->storage);

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_cleanup(struct mm_event_receiver *receiver)
{
	ENTER();

	// Release forward buffers.
	mm_common_free(receiver->forward_buffers);
	mm_bitset_cleanup(&receiver->forward_targets, &mm_common_space.xarena);

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_poll_start(struct mm_event_receiver *receiver)
{
	ENTER();

	// No events arrived yet.
	receiver->got_events = false;
	receiver->direct_events = 0;
	receiver->enqueued_events = 0;
	receiver->dequeued_events = 0;
	receiver->forwarded_events = 0;

	// Start a reclamation-critical section.
	if (!receiver->reclaim_active) {
		mm_memory_store(receiver->reclaim_active, true);
		mm_memory_strict_fence();
		// Catch up with the current reclamation epoch.
		uint32_t epoch = mm_memory_load(receiver->dispatch->reclaim_epoch);
		mm_memory_store(receiver->reclaim_epoch, epoch);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_poll_finish(struct mm_event_receiver *receiver)
{
	ENTER();

	struct mm_event_dispatch *dispatch = receiver->dispatch;

	// Count directly handled events.
	if (receiver->direct_events || receiver->stats.dequeued_events)
		receiver->got_events = true;

	receiver->stats.direct_events += receiver->direct_events;
	receiver->stats.enqueued_events += receiver->enqueued_events;
	receiver->stats.dequeued_events += receiver->dequeued_events;

	// Flush and count forwarded events.
	if (receiver->forwarded_events) {
		receiver->got_events = true;
		receiver->stats.forwarded_events += receiver->forwarded_events;

		mm_thread_t target = mm_bitset_find(&receiver->forward_targets, 0);
		while (target != MM_THREAD_NONE) {
			struct mm_event_listener *listener = &dispatch->listeners[target];
			mm_event_receiver_forward_flush(listener->thread,
							&receiver->forward_buffers[target]);

			if (++target < mm_bitset_size(&receiver->forward_targets))
				target = mm_bitset_find(&receiver->forward_targets, target);
			else
				target = MM_THREAD_NONE;
		}

		mm_bitset_clear_all(&receiver->forward_targets);
	}

	// Advance the reclamation epoch.
	mm_event_receiver_observe_epoch(receiver);

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_dispatch_start(struct mm_event_receiver *receiver, uint32_t nevents)
{
	ENTER();

	struct mm_event_dispatch *dispatch = receiver->dispatch;

	mm_regular_lock(&receiver->dispatch->event_sink_lock);

	uint32_t nr = receiver->direct_events + receiver->dequeued_events;
	uint32_t nq = dispatch->sink_queue_tail - dispatch->sink_queue_head;
	while ((nq + nevents) > dispatch->sink_queue_size || (nq > 0 && nr < MM_EVENT_RECEIVER_STEAL_THRESHOLD)) {
		mm_event_receiver_dequeue(receiver, dispatch);
		nr++;
		nq--;
	}

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_dispatch_finish(struct mm_event_receiver *receiver)
{
	ENTER();

	mm_regular_unlock(&receiver->dispatch->event_sink_lock);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_dispatch(struct mm_event_receiver *receiver, struct mm_event_fd *sink, mm_event_t event)
{
	ENTER();
	ASSERT(receiver->thread == mm_thread_self());

	if (sink->loose_target) {
		// Handle the event immediately.
		mm_event_convey(sink, event);
		receiver->stats.loose_events++;

	} else {
		// If the event sink can be detached then do it now.
		if (!sink->bound_target && !mm_event_active(sink))
			sink->target = MM_THREAD_NONE;

		// Count the received event.
		mm_event_update_receive_stamp(sink);

		// If the event sink is detached perhaps the current thread
		// deserves to steal it.
		mm_thread_t target = mm_event_target(sink);
		if (target == MM_THREAD_NONE) {
			uint32_t nr = receiver->direct_events + receiver->dequeued_events;
			if (nr < MM_EVENT_RECEIVER_STEAL_THRESHOLD) {
				mm_event_receiver_restore(receiver, sink);
				target = sink->target;
			}
		}

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == receiver->thread) {
			mm_event_convey(sink, event);
			receiver->direct_events++;
		} else if (target == MM_THREAD_NONE) {
			mm_event_receiver_enqueue(receiver, sink, event);
		} else {
			mm_event_receiver_forward(receiver, sink, event);
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_unregister(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	mm_event_update_receive_stamp(sink);
	mm_event_receiver_reclaim_queue_insert(receiver, sink);
	mm_event_convey(sink, MM_EVENT_DISABLE);

	LEAVE();
}
