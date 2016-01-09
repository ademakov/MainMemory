/*
 * base/event/receiver.c - MainMemory event receiver.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "base/event/dispatch.h"
#include "base/log/debug.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"

/**********************************************************************
 * Event forward request handlers.
 **********************************************************************/

static void
mm_event_receiver_forward_1(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];

	// Handle events.
	mm_event_convey(sink_1);

	LEAVE();
}

static void
mm_event_receiver_forward_2(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];

	// Handle events.
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);

	LEAVE();
}

static void
mm_event_receiver_forward_3(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[2];

	// Handle events.
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);
	mm_event_convey(sink_3);

	LEAVE();
}

static void
mm_event_receiver_forward_4(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[2];
	struct mm_event_fd *sink_4 = (struct mm_event_fd *) arguments[3];

	// Handle events.
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);
	mm_event_convey(sink_3);
	mm_event_convey(sink_4);

	LEAVE();
}

/**********************************************************************
 * Event publish request handlers.
 **********************************************************************/

#if ENABLE_EVENT_PUBLISH

static void
mm_event_receiver_publish_1(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];

	// Handle events.
	sink_1->target = context;
	mm_event_convey(sink_1);

	LEAVE();
}

static void
mm_event_receiver_publish_2(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];

	// Handle events.
	sink_1->target = context;
	sink_2->target = context;
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);

	LEAVE();
}

static void
mm_event_receiver_publish_3(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[2];

	// Handle events.
	sink_1->target = context;
	sink_2->target = context;
	sink_3->target = context;
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);
	mm_event_convey(sink_3);

	LEAVE();
}

static void
mm_event_receiver_publish_4(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[2];
	struct mm_event_fd *sink_4 = (struct mm_event_fd *) arguments[3];

	// Handle events.
	sink_1->target = context;
	sink_2->target = context;
	sink_3->target = context;
	sink_4->target = context;
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);
	mm_event_convey(sink_3);
	mm_event_convey(sink_4);

	LEAVE();
}

#endif

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
		mm_thread_send_1(thread, mm_event_receiver_forward_1,
				 (uintptr_t) buffer->sinks[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_thread_send_2(thread, mm_event_receiver_forward_2,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1]);
		break;
	case 3:
		buffer->nsinks = 0;
		mm_thread_send_3(thread, mm_event_receiver_forward_3,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2]);
		break;
	case 4:
		buffer->nsinks = 0;
		mm_thread_send_4(thread, mm_event_receiver_forward_4,
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

#if ENABLE_EVENT_PUBLISH

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
		mm_domain_send_1(domain, mm_event_receiver_publish_1,
				 (uintptr_t) buffer->sinks[0]);
		break;
	case 2:
		buffer->nsinks = 0;
		mm_domain_send_2(domain, mm_event_receiver_publish_2,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1]);
		break;
	case 3:
		buffer->nsinks = 0;
		mm_domain_send_3(domain, mm_event_receiver_publish_3,
				 (uintptr_t) buffer->sinks[0],
				 (uintptr_t) buffer->sinks[1],
				 (uintptr_t) buffer->sinks[2]);
		break;
	case 4:
		buffer->nsinks = 0;
		mm_domain_send_4(domain, mm_event_receiver_publish_4,
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

#endif

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
		ASSERT(sink->unregister_phase == MM_EVENT_CLEANUP);
		sink->unregister_phase = MM_EVENT_RECLAIM;
		mm_event_convey(sink);
	}
}

void NONNULL(1)
mm_event_receiver_observe_epoch(struct mm_event_receiver *receiver)
{
	uint32_t epoch = mm_memory_load(receiver->dispatch->reclaim_epoch);
	if (receiver->reclaim_epoch != epoch) {
		ASSERT((receiver->reclaim_epoch + 1) == epoch);
		mm_memory_store(receiver->reclaim_epoch, epoch);
		mm_event_receiver_reclaim_epoch(receiver, epoch);
		mm_dispatch_advance_epoch(receiver->dispatch);
	}
}

/**********************************************************************
 * Event receiver.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_receiver_prepare(struct mm_event_receiver *receiver, struct mm_dispatch *dispatch,
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

#if ENABLE_EVENT_PUBLISH
	// Prepare publish buffer.
	mm_event_receiver_pubbuf_prepare(&receiver->publish_buffer);
#endif

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

	// Initialize flags that indicate event arrival.
	receiver->got_events = false;
#if ENABLE_EVENT_PUBLISH
	receiver->published_events = false;
#endif
	mm_bitset_clear_all(&receiver->forward_targets);

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

	struct mm_dispatch *dispatch = receiver->dispatch;

#if ENABLE_EVENT_PUBLISH
	// Flush published events.
	if (receiver->published_events) {
		mm_event_receiver_publish_flush(dispatch->domain, &receiver->publish_buffer);
		mm_dispatch_notify_waiting(dispatch);
	}
#endif

	// Flush forwarded events.
	mm_thread_t target = mm_bitset_find(&receiver->forward_targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_event_listener *listener = &dispatch->listeners[target];
		mm_event_receiver_forward_flush(listener->thread,
						&receiver->forward_buffers[target]);
		mm_event_listener_notify(listener);

		if (++target < mm_bitset_size(&receiver->forward_targets))
			target = mm_bitset_find(&receiver->forward_targets, target);
		else
			target = MM_THREAD_NONE;
	}

	// Advance the reclamation epoch.
	mm_event_receiver_observe_epoch(receiver);

	// Finish a reclamation-critical section.
	if (mm_event_receiver_reclaim_queue_empty(receiver)) {
		mm_memory_store_fence();
		mm_memory_store(receiver->reclaim_active, false);
	}

	LEAVE();
}

static void NONNULL(1, 2)
mm_event_receiver_handle(struct mm_event_receiver *receiver, struct mm_event_fd *sink,
			 uint32_t expected_iostate)
{
	ENTER();
	ASSERT(receiver->thread == mm_thread_self());

	receiver->got_events = true;

	if (sink->loose_target) {
		// Handle the event immediately.
		mm_event_convey(sink);

	} else {
		mm_thread_t target = sink->target;

		// If the event sink is detached then attach it to the control
		// thread.
		if (!sink->bound_target && target != receiver->thread) {
			uint32_t iostate = mm_memory_load(sink->io.state);
			mm_memory_load_fence();
			uint8_t attached = mm_memory_load(sink->attached);
			if (!attached
			    && iostate == expected_iostate
			    && sink->unregister_phase == MM_EVENT_NONE) {
#if ENABLE_EVENT_PUBLISH
				target = sink->target = MM_THREAD_NONE;
#else
				target = sink->target = receiver->thread;
#endif
			}
		}

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == receiver->thread) {
			mm_event_convey(sink);

		} else if (target != MM_THREAD_NONE) {
			struct mm_dispatch *dispatch = receiver->dispatch;
			struct mm_event_listener *listener = &dispatch->listeners[target];
			mm_event_receiver_forward(listener->thread,
						  &receiver->forward_buffers[target], sink);
			mm_bitset_set(&receiver->forward_targets, target);

		} else {
#if ENABLE_EVENT_PUBLISH
			// TODO: BUG!!! If this is done more than once for
			// a single sink then there might be a big problem.
			// This must be resolved before any production use.
			// Therefore this is just a temporary stub !!!
			mm_event_receiver_publish(receiver->dispatch->domain,
						  &receiver->publish_buffer, sink);
			receiver->published_events = true;
#else
			ABORT();
#endif
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
	mm_event_receiver_handle(receiver, sink, 0);

	LEAVE();
}
