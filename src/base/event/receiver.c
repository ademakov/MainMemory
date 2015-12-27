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
#include "base/log/debug.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"
#include "base/thread/thread.h"

void NONNULL(1, 3)
mm_event_receiver_prepare(struct mm_event_receiver *receiver, mm_thread_t nthreads,
			  struct mm_thread *threads[])
{
	ENTER();

	// Allocate listener info.
	receiver->nlisteners = nthreads;
	receiver->listeners = mm_common_calloc(nthreads, sizeof(struct mm_event_listener));
	for (mm_thread_t i = 0; i < nthreads; i++)
		mm_event_listener_prepare(&receiver->listeners[i], threads[i]);

	mm_bitset_prepare(&receiver->targets, &mm_common_space.xarena, nthreads);

	receiver->nsinks = 0;

	LEAVE();
}

void NONNULL(1)
mm_event_receiver_cleanup(struct mm_event_receiver *receiver)
{
	ENTER();

	// Release listener info.
	for (mm_thread_t i = 0; i < receiver->nlisteners; i++)
		mm_event_listener_cleanup(&receiver->listeners[i]);
	mm_common_free(receiver->listeners);

	mm_bitset_cleanup(&receiver->targets, &mm_common_space.xarena);

	LEAVE();
}

static void
mm_event_receiver_handle_1_req(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[0];
	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[1];

	if (listener == NULL)
		sink_1->target = context;

	// Handle events.
	mm_event_convey(sink_1);

	LEAVE();
}

static void
mm_event_receiver_handle_2_req(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[0];
	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[2];

	if (listener == NULL) {
		sink_1->target = context;
		sink_2->target = context;
	}

	// Handle events.
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);

	LEAVE();
}

static void
mm_event_receiver_handle_3_req(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[0];
	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[2];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[3];

	if (listener == NULL) {
		sink_1->target = context;
		sink_2->target = context;
		sink_3->target = context;
	}

	// Handle events.
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);
	mm_event_convey(sink_3);

	LEAVE();
}

static void
mm_event_receiver_handle_4_req(uintptr_t context, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[0];
	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[2];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[3];
	struct mm_event_fd *sink_4 = (struct mm_event_fd *) arguments[4];

	if (listener == NULL) {
		sink_1->target = context;
		sink_2->target = context;
		sink_3->target = context;
		sink_4->target = context;
	}

	// Handle events.
	mm_event_convey(sink_1);
	mm_event_convey(sink_2);
	mm_event_convey(sink_3);
	mm_event_convey(sink_4);

	LEAVE();
}

static void
mm_event_receiver_forward(struct mm_event_listener *listener, struct mm_event_fd *sink)
{
	ENTER();

	// Flush the buffer if it is full.
	if (listener->nsinks == 4) {
		mm_thread_send_5(listener->thread,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) listener,
				 (uintptr_t) listener->sinks[0],
				 (uintptr_t) listener->sinks[1],
				 (uintptr_t) listener->sinks[2],
				 (uintptr_t) listener->sinks[3]);
		listener->nsinks = 0;
	}

	// Add the sink to the buffer.
	listener->sinks[listener->nsinks++] = sink;

	LEAVE();
}

static void
mm_event_receiver_publish(struct mm_event_receiver *reciever, struct mm_event_fd *sink)
{
	ENTER();

	// Flush the buffer if it is full.
	if (reciever->nsinks == 4) {
		mm_domain_send_5(mm_regular_domain,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) NULL,
				 (uintptr_t) reciever->sinks[0],
				 (uintptr_t) reciever->sinks[1],
				 (uintptr_t) reciever->sinks[2],
				 (uintptr_t) reciever->sinks[3]);
		reciever->nsinks = 0;
	}

	// Add the sink to the buffer.
	reciever->sinks[reciever->nsinks++] = sink;

	LEAVE();
}

static void
mm_event_receiver_forward_flush(struct mm_event_listener *listener)
{
	ENTER();

	switch (listener->nsinks) {
	case 1:
		mm_thread_send_2(listener->thread,
				 mm_event_receiver_handle_1_req,
				 (uintptr_t) listener,
				 (uintptr_t) listener->sinks[0]);
		break;
	case 2:
		mm_thread_send_3(listener->thread,
				 mm_event_receiver_handle_2_req,
				 (uintptr_t) listener,
				 (uintptr_t) listener->sinks[0],
				 (uintptr_t) listener->sinks[1]);
		break;
	case 3:
		mm_thread_send_4(listener->thread,
				 mm_event_receiver_handle_3_req,
				 (uintptr_t) listener,
				 (uintptr_t) listener->sinks[0],
				 (uintptr_t) listener->sinks[1],
				 (uintptr_t) listener->sinks[2]);
		break;
	case 4:
		mm_thread_send_5(listener->thread,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) listener,
				 (uintptr_t) listener->sinks[0],
				 (uintptr_t) listener->sinks[1],
				 (uintptr_t) listener->sinks[2],
				 (uintptr_t) listener->sinks[3]);
		break;
	default:
		ABORT();
	}

	listener->nsinks = 0;

	LEAVE();
}

static void
mm_event_receiver_publish_flush(struct mm_event_receiver *receiver)
{
	ENTER();

	switch (receiver->nsinks) {
	case 1:
		mm_domain_send_2(mm_regular_domain,
				 mm_event_receiver_handle_1_req,
				 (uintptr_t) NULL,
				 (uintptr_t) receiver->sinks[0]);
		break;
	case 2:
		mm_domain_send_3(mm_regular_domain,
				 mm_event_receiver_handle_2_req,
				 (uintptr_t) NULL,
				 (uintptr_t) receiver->sinks[0],
				 (uintptr_t) receiver->sinks[1]);
		break;
	case 3:
		mm_domain_send_4(mm_regular_domain,
				 mm_event_receiver_handle_3_req,
				 (uintptr_t) NULL,
				 (uintptr_t) receiver->sinks[0],
				 (uintptr_t) receiver->sinks[1],
				 (uintptr_t) receiver->sinks[2]);
		break;
	case 4:
		mm_domain_send_5(mm_regular_domain,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) NULL,
				 (uintptr_t) receiver->sinks[0],
				 (uintptr_t) receiver->sinks[1],
				 (uintptr_t) receiver->sinks[2],
				 (uintptr_t) receiver->sinks[3]);
		break;
	default:
		ABORT();
	}

	receiver->nsinks = 0;

	LEAVE();
}

void NONNULL(1, 3)
mm_event_receiver_listen(struct mm_event_receiver *receiver, mm_thread_t thread,
			 struct mm_event_backend *backend, mm_timeout_t timeout)
{
	ENTER();

	receiver->got_events = false;
	receiver->published_events = false;
	receiver->control_thread = thread;
	mm_bitset_clear_all(&receiver->targets);

	// Poll for incoming events or wait for timeout expiration.
	struct mm_event_listener *listener = &receiver->listeners[thread];
	mm_event_listener_poll(listener, backend, receiver, timeout);

	// Flush published events.
	if (receiver->published_events) {
		if (receiver->nsinks)
			mm_event_receiver_publish_flush(receiver);
		mm_even_receiver_notify_waiting(receiver, backend);
	}

	// Forward incoming events that belong to other threads.
	mm_thread_t target = mm_bitset_find(&receiver->targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_event_listener *target_listener = &receiver->listeners[target];

		// Flush forwarded events.
		mm_event_receiver_forward_flush(target_listener);

		// Wake up the target thread if it is sleeping.
		mm_event_listener_notify(target_listener, backend);

		if (++target < mm_bitset_size(&receiver->targets))
			target = mm_bitset_find(&receiver->targets, target);
		else
			target = MM_THREAD_NONE;
	}

	LEAVE();
}

static void NONNULL(1, 2)
mm_event_receiver_handle(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
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
			uint32_t dispatch_stamp = mm_memory_load(sink->dispatch_stamp);
			mm_memory_load_fence();
			uint8_t attached = mm_memory_load(sink->attached);
			if (dispatch_stamp == sink->arrival_stamp && !attached) {
				sink->target = MM_THREAD_NONE;
				target = MM_THREAD_NONE;
			}
		}

		// Update the arrival stamp. This disables the event sink
		// stealing until the event jumps through all the hoops and
		// the dispatch stamp is updated accordingly.
		sink->arrival_stamp++;

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == receiver->control_thread) {
			mm_event_convey(sink);

		} else if (target != MM_THREAD_NONE) {
			struct mm_event_listener *listener = &receiver->listeners[target];
			mm_event_receiver_forward(listener, sink);
			mm_bitset_set(&receiver->targets, target);

		} else {
			// TODO: BUG!!! If this is done more than once for
			// a single sink then there might be a big problem.
			// This must be resolved before any production use.
			// Therefore this is just a temporary stub !!!
			mm_event_receiver_publish(receiver, sink);
			receiver->published_events = true;
		}
	}

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_input(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	sink->io.input.ready = 1;
	sink->oneshot_input_trigger = 0;
	mm_event_receiver_handle(receiver, sink);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_input_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	sink->io.input.error = 1;
	sink->oneshot_input_trigger = 0;
	mm_event_receiver_handle(receiver, sink);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_output(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	sink->io.output.ready = 1;
	sink->oneshot_output_trigger = 0;
	mm_event_receiver_handle(receiver, sink);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_output_error(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	sink->io.output.error = 1;
	sink->oneshot_output_trigger = 0;
	mm_event_receiver_handle(receiver, sink);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_receiver_unregister(struct mm_event_receiver *receiver, struct mm_event_fd *sink)
{
	ENTER();

	sink->state = MM_EVENT_UNREGISTERED;
	mm_event_receiver_handle(receiver, sink);

	LEAVE();
}

void NONNULL(1, 2)
mm_even_receiver_notify_waiting(struct mm_event_receiver *receiver,
				struct mm_event_backend *backend)
{
	ENTER();

	mm_thread_t n = receiver->nlisteners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_event_listener *listener = &receiver->listeners[i];
		if (mm_event_listener_getstate(listener) == MM_EVENT_LISTENER_WAITING) {
			mm_event_listener_notify(listener, backend);
			break;
		}
	}

	LEAVE();
}
