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

#include "base/base.h"
#include "base/event/batch.h"
#include "base/event/dispatch.h"
#include "base/log/debug.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"
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

/**********************************************************************
 * Event receiver.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_receiver_prepare(struct mm_event_receiver *receiver, struct mm_dispatch *dispatch)
{
	ENTER();

	// Remember the top-level owner.
	receiver->dispatch = dispatch;

	// Prepare forward buffers.
	receiver->forward_buffers = mm_common_calloc(dispatch->nlisteners,
						     sizeof(struct mm_event_receiver_fwdbuf));
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_event_receiver_fwdbuf_prepare(&receiver->forward_buffers[i]);

	mm_bitset_prepare(&receiver->targets, &mm_common_space.xarena, dispatch->nlisteners);

	// Prepare publish buffer.
	mm_event_receiver_pubbuf_prepare(&receiver->publish_buffer);

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_cleanup(struct mm_event_receiver *receiver)
{
	ENTER();

	mm_common_free(receiver->forward_buffers);

	mm_bitset_cleanup(&receiver->targets, &mm_common_space.xarena);

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_listen(struct mm_event_receiver *receiver, mm_thread_t thread,
			 mm_timeout_t timeout)
{
	ENTER();

	receiver->got_events = false;
	receiver->published_events = false;
	receiver->control_thread = thread;
	mm_bitset_clear_all(&receiver->targets);

	// Poll for incoming events or wait for timeout expiration.
	struct mm_dispatch *dispatch = receiver->dispatch;
	struct mm_event_listener *listener = &dispatch->listeners[thread];
	mm_event_listener_poll(listener, &dispatch->backend, receiver, timeout);

	// Flush published events.
	if (receiver->published_events) {
		mm_event_receiver_publish_flush(mm_regular_domain, &receiver->publish_buffer);
		mm_dispatch_notify_waiting(dispatch);
	}

	// Forward incoming events that belong to other threads.
	mm_thread_t target = mm_bitset_find(&receiver->targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_event_listener *target_listener = &dispatch->listeners[target];

		// Flush forwarded events.
		mm_event_receiver_forward_flush(target_listener->thread,
						&receiver->forward_buffers[target]);

		// Wake up the target thread if it is sleeping.
		mm_event_listener_notify(target_listener, &dispatch->backend);

		if (++target < mm_bitset_size(&receiver->targets))
			target = mm_bitset_find(&receiver->targets, target);
		else
			target = MM_THREAD_NONE;
	}

	LEAVE();
}

static void NONNULL(1, 2)
mm_event_receiver_handle(struct mm_event_receiver *receiver, struct mm_event_fd *sink,
			 uint32_t expected_iostate)
{
	ENTER();
	ASSERT(receiver->control_thread == mm_thread_self());

	receiver->got_events = true;

	if (sink->loose_target) {
		// Handle the event immediately.
		mm_event_convey(sink);

	} else {
		mm_thread_t target = sink->target;

		// If the event sink is detached then attach it to the control
		// thread.
		if (!sink->bound_target && target != receiver->control_thread) {
			uint32_t iostate = mm_memory_load(sink->io.state);
			mm_memory_load_fence();
			uint8_t attached = mm_memory_load(sink->attached);
			if (!attached
			    && iostate == expected_iostate
			    && sink->state != MM_EVENT_UNREGISTERED) {
				sink->target = MM_THREAD_NONE;
				target = MM_THREAD_NONE;
			}
		}

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == receiver->control_thread) {
			mm_event_convey(sink);

		} else if (target != MM_THREAD_NONE) {
			struct mm_dispatch *dispatch = receiver->dispatch;
			struct mm_event_listener *listener = &dispatch->listeners[target];
			mm_event_receiver_forward(listener->thread,
						  &receiver->forward_buffers[target], sink);
			mm_bitset_set(&receiver->targets, target);

		} else {
			// TODO: BUG!!! If this is done more than once for
			// a single sink then there might be a big problem.
			// This must be resolved before any production use.
			// Therefore this is just a temporary stub !!!
			mm_event_receiver_publish(mm_regular_domain,
						  &receiver->publish_buffer, sink);
			receiver->published_events = true;
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

	sink->state = MM_EVENT_UNREGISTERED;
	mm_event_receiver_handle(receiver, sink, 0);

	LEAVE();
}
