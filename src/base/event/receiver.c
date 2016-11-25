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
	mm_event_convey((struct mm_event_fd *) arguments[0]);

	LEAVE();
}

static void
mm_event_receiver_forward_2(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_event_convey((struct mm_event_fd *) arguments[0]);
	mm_event_convey((struct mm_event_fd *) arguments[1]);

	LEAVE();
}

static void
mm_event_receiver_forward_3(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_event_convey((struct mm_event_fd *) arguments[0]);
	mm_event_convey((struct mm_event_fd *) arguments[1]);
	mm_event_convey((struct mm_event_fd *) arguments[2]);

	LEAVE();
}

static void
mm_event_receiver_forward_4(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_event_convey((struct mm_event_fd *) arguments[0]);
	mm_event_convey((struct mm_event_fd *) arguments[1]);
	mm_event_convey((struct mm_event_fd *) arguments[2]);
	mm_event_convey((struct mm_event_fd *) arguments[3]);

	LEAVE();
}

static void
mm_event_receiver_forward_5(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_event_convey((struct mm_event_fd *) arguments[0]);
	mm_event_convey((struct mm_event_fd *) arguments[1]);
	mm_event_convey((struct mm_event_fd *) arguments[2]);
	mm_event_convey((struct mm_event_fd *) arguments[3]);
	mm_event_convey((struct mm_event_fd *) arguments[4]);

	LEAVE();
}

static void
mm_event_receiver_forward_6(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_event_convey((struct mm_event_fd *) arguments[0]);
	mm_event_convey((struct mm_event_fd *) arguments[1]);
	mm_event_convey((struct mm_event_fd *) arguments[2]);
	mm_event_convey((struct mm_event_fd *) arguments[3]);
	mm_event_convey((struct mm_event_fd *) arguments[4]);
	mm_event_convey((struct mm_event_fd *) arguments[5]);

	LEAVE();
}

/**********************************************************************
 * Event publish request handlers.
 **********************************************************************/

static void
mm_event_receiver_convey(struct mm_event_fd *sink, mm_thread_t target)
{
	sink->target = target;
	mm_event_convey(sink);
}

static void
mm_event_receiver_publish_1(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_thread_t target = mm_thread_self();
	mm_event_receiver_convey((struct mm_event_fd *) arguments[0], target);

	LEAVE();
}

static void
mm_event_receiver_publish_2(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_thread_t target = mm_thread_self();
	mm_event_receiver_convey((struct mm_event_fd *) arguments[0], target);
	mm_event_receiver_convey((struct mm_event_fd *) arguments[1], target);

	LEAVE();
}

static void
mm_event_receiver_publish_3(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_thread_t target = mm_thread_self();
	mm_event_receiver_convey((struct mm_event_fd *) arguments[0], target);
	mm_event_receiver_convey((struct mm_event_fd *) arguments[1], target);
	mm_event_receiver_convey((struct mm_event_fd *) arguments[2], target);

	LEAVE();
}

static void
mm_event_receiver_publish_4(uintptr_t *arguments)
{
	ENTER();

	// Handle events.
	mm_thread_t target = mm_thread_self();
	mm_event_receiver_convey((struct mm_event_fd *) arguments[0], target);
	mm_event_receiver_convey((struct mm_event_fd *) arguments[1], target);
	mm_event_receiver_convey((struct mm_event_fd *) arguments[2], target);
	mm_event_receiver_convey((struct mm_event_fd *) arguments[3], target);

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
		mm_thread_post_1(thread, mm_event_receiver_forward_1,
				 (uintptr_t) buffer->sinks[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_thread_post_2(thread, mm_event_receiver_forward_2,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1]);
		break;
	case 3:
		buffer->nsinks = 0;
		mm_thread_post_3(thread, mm_event_receiver_forward_3,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2]);
		break;
	case 4:
		buffer->nsinks = 0;
		mm_thread_post_4(thread, mm_event_receiver_forward_4,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3]);
		break;
	case 5:
		buffer->nsinks = 0;
		mm_thread_post_5(thread, mm_event_receiver_forward_5,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3],
				 (uintptr_t) buffer->sinks[4]);
		break;
	case 6:
		buffer->nsinks = 0;
		mm_thread_post_6(thread, mm_event_receiver_forward_6,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3],
				 (uintptr_t) buffer->sinks[4],
				 (uintptr_t) buffer->sinks[5]);
		break;
	default:
		ABORT();
	}

	LEAVE();
}

static void
mm_event_receiver_forward(struct mm_thread *thread, struct mm_event_receiver_fwdbuf *buffer,
			  struct mm_event_fd *sink)
{
	ENTER();

	// Flush the buffer if it is full.
	if (buffer->nsinks == MM_EVENT_RECEIVER_FWDBUF_SIZE)
		mm_event_receiver_forward_flush(thread, buffer);

	// Add the sink to the buffer.
	buffer->sinks[buffer->nsinks++] = sink;

	LEAVE();
}

/**********************************************************************
 * Event publishing.
 **********************************************************************/

static void
mm_event_receiver_pubbuf_prepare(struct mm_event_receiver_pubbuf *buffer)
{
	buffer->nsinks = 0;
}

static void
mm_event_receiver_publish_flush(struct mm_domain *domain, struct mm_event_receiver_pubbuf *buffer)
{
	ENTER();

	switch (buffer->nsinks) {
	case 0:
		break;
	case 1:
		buffer->nsinks = 0;
		mm_domain_post_1(domain, mm_event_receiver_publish_1,
				 (uintptr_t) buffer->sinks[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_domain_post_2(domain, mm_event_receiver_publish_2,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1]);
		break;
	case 3:
		buffer->nsinks = 0;
		mm_domain_post_3(domain, mm_event_receiver_publish_3,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2]);
		break;
	case 4:
		buffer->nsinks = 0;
		mm_domain_post_4(domain, mm_event_receiver_publish_4,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2],
				 (uintptr_t) buffer->sinks[3]);
		break;
	default:
		ABORT();
	}

	LEAVE();
}

static void
mm_event_receiver_publish(struct mm_domain *domain, struct mm_event_receiver_pubbuf *buffer,
			  struct mm_event_fd *sink)
{
	ENTER();

	// Flush the buffer if it is full.
	if (buffer->nsinks == MM_EVENT_RECEIVER_PUBBUF_SIZE)
		mm_event_receiver_publish_flush(domain, buffer);

	// Add the sink to the buffer.
	buffer->sinks[buffer->nsinks++] = sink;

	LEAVE();
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
		ASSERT(sink->unregister_phase == MM_EVENT_DISABLE);
		sink->unregister_phase = MM_EVENT_RECLAIM;
		mm_event_convey(sink);
	}
}

void NONNULL(1)
mm_event_receiver_observe_epoch(struct mm_event_receiver *receiver)
{
	ENTER();

	// Reclaim queued event sinks associated with a past epoch.
	uint32_t epoch = mm_memory_load(receiver->dispatch->reclaim_epoch);
	if (receiver->reclaim_epoch != epoch) {
		VERIFY((receiver->reclaim_epoch + 1) == epoch);
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
	receiver->reclaim_active = false;
	receiver->reclaim_epoch = dispatch->reclaim_epoch;
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

	// Prepare the publish buffer.
	mm_event_receiver_pubbuf_prepare(&receiver->publish_buffer);

	// Initialize event statistics.
	receiver->stats.loose_events = 0;
	receiver->stats.direct_events = 0;
	receiver->stats.stolen_events = 0;
	receiver->stats.forwarded_events = 0;
	receiver->stats.published_events = 0;

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
mm_event_receiver_start(struct mm_event_receiver *receiver)
{
	ENTER();

	// No events arrived yet.
	receiver->got_events = false;
	receiver->direct_events_estimate = 0;
	receiver->direct_events = 0;
	receiver->stolen_events = 0;
	receiver->forwarded_events = 0;
	receiver->published_events = 0;

	// Start a reclamation-critical section.
	if (!receiver->reclaim_active) {
		mm_memory_store(receiver->reclaim_active, true);
		mm_memory_store_fence();
		// Catch up with the current reclamation epoch.
		uint32_t epoch = mm_memory_load(receiver->dispatch->reclaim_epoch);
		mm_memory_store(receiver->reclaim_epoch, epoch);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_finish(struct mm_event_receiver *receiver)
{
	ENTER();

	struct mm_event_dispatch *dispatch = receiver->dispatch;

	// Count directly handled events.
	if (receiver->direct_events) {
		receiver->got_events = true;
		receiver->stats.direct_events += receiver->direct_events;
		receiver->stats.stolen_events += receiver->stolen_events;
	}

	// Flush and count published events.
	if (receiver->published_events) {
		receiver->got_events = true;
		receiver->stats.published_events += receiver->published_events;

		mm_event_receiver_publish_flush(dispatch->domain, &receiver->publish_buffer);
		mm_event_dispatch_notify_waiting(dispatch);
	}

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

static bool NONNULL(1, 2)
mm_event_receiver_stealable(struct mm_event_receiver *receiver, struct mm_event_fd *sink,
			    uint32_t expected_iostate, mm_thread_t target)
{
	if (target == MM_THREAD_NONE)
		return true;
	if (target == receiver->thread || sink->bound_target)
		return false;
	if (receiver->direct_events >= MM_EVENT_RECEIVER_STEAL_THRESHOLD)
		return false;
	if (receiver->direct_events_estimate >= MM_EVENT_RECEIVER_STEAL_THRESHOLD)
		return false;

	uint32_t iostate = mm_memory_load(sink->io.state);
	mm_memory_load_fence();
	uint8_t attached = mm_memory_load(sink->attached);

	return !attached && iostate == expected_iostate;
}

static void NONNULL(1, 2)
mm_event_receiver_handle(struct mm_event_receiver *receiver, struct mm_event_fd *sink,
			 uint32_t expected_iostate)
{
	ENTER();
	ASSERT(receiver->thread == mm_thread_self());

	if (sink->loose_target) {
		// Handle the event immediately.
		mm_event_convey(sink);
		receiver->stats.loose_events++;

	} else {
		mm_thread_t target = mm_event_target(sink);

		// If the event sink should be stolen then attach it to the
		// control thread.
		if (mm_event_receiver_stealable(receiver, sink, expected_iostate, target)) {
#if 0
			if (receiver->direct_events < 5) {
				target = sink->target = receiver->thread;
				receiver->stolen_events++;
			} else {
				target = sink->target = MM_THREAD_NONE;
			}
#else
			target = sink->target = receiver->thread;
			receiver->stolen_events++;
#endif
		}

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == receiver->thread) {
			mm_event_convey(sink);
			receiver->direct_events++;

		} else if (target != MM_THREAD_NONE) {
			struct mm_event_dispatch *dispatch = receiver->dispatch;
			struct mm_event_listener *listener = &dispatch->listeners[target];
			mm_event_receiver_forward(listener->thread,
						  &receiver->forward_buffers[target], sink);
			mm_bitset_set(&receiver->forward_targets, target);
			receiver->forwarded_events++;

		} else {
			// TODO: BUG!!! If this is done more than once for
			// a single sink then there might be a big problem.
			// This must be resolved before any production use.
			// Therefore this is just a temporary stub !!!
			mm_event_receiver_publish(receiver->dispatch->domain,
						  &receiver->publish_buffer, sink);
			receiver->published_events++;
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_input(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	static const mm_event_iostate_t iostate = {{
		.input = { .error = 0, .ready = 1, },
		.output = { .error = 0, .ready = 0, },
	}};

	sink->io.input.ready = 1;
	sink->oneshot_input_trigger = 0;
	mm_event_receiver_handle(receiver, sink, iostate.state);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_input_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	static const mm_event_iostate_t iostate = {{
		.input = { .error = 1, .ready = 0, },
		.output = { .error = 0, .ready = 0, },
	}};

	sink->io.input.error = 1;
	sink->oneshot_input_trigger = 0;
	mm_event_receiver_handle(receiver, sink, iostate.state);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_output(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	static const mm_event_iostate_t iostate = {{
		.input = { .error = 0, .ready = 0, },
		.output = { .error = 0, .ready = 1, },
	}};

	sink->io.output.ready = 1;
	sink->oneshot_output_trigger = 0;
	mm_event_receiver_handle(receiver, sink, iostate.state);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_output_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	static const mm_event_iostate_t iostate = {{
		.input = { .error = 0, .ready = 0, },
		.output = { .error = 1, .ready = 0, },
	}};

	sink->io.output.error = 1;
	sink->oneshot_output_trigger = 0;
	mm_event_receiver_handle(receiver, sink, iostate.state);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_unregister(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	sink->unregister_phase = MM_EVENT_DISABLE;
	mm_event_receiver_reclaim_queue_insert(receiver, sink);
	mm_event_convey(sink);

	LEAVE();
}
