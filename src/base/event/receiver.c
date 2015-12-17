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
	receiver->listeners = mm_common_calloc(nthreads,
					       sizeof(struct mm_event_listener));
	for (mm_thread_t i = 0; i < nthreads; i++)
		mm_event_listener_prepare(&receiver->listeners[i], threads[i]);

	mm_bitset_prepare(&receiver->targets, &mm_common_space.xarena,
			  nthreads);

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
mm_event_receiver_handle_1_req(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	mm_event_t event_1 = arguments[1];

	// Handle events.
	mm_event_handle(sink_1, event_1);

	// Update private event stamp.
	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[2];
	listener->delivery_stamp += 1;

	LEAVE();
}

static void
mm_event_receiver_handle_2_req(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];
	mm_event_t event_1 = arguments[2] & 0xff;
	mm_event_t event_2 = (arguments[2] >> 8) & 0xff;

	// Handle events.
	mm_event_handle(sink_1, event_1);
	mm_event_handle(sink_2, event_2);

	// Update private event stamp.
	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[3];
	listener->delivery_stamp += 2;

	LEAVE();
}

static void
mm_event_receiver_handle_3_req(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[2];
	mm_event_t event_1 = arguments[3] & 0xff;
	mm_event_t event_2 = (arguments[3] >> 8) & 0xff;
	mm_event_t event_3 = (arguments[3] >> 16) & 0xff;

	// Handle events.
	mm_event_handle(sink_1, event_1);
	mm_event_handle(sink_2, event_2);
	mm_event_handle(sink_3, event_3);

	// Update private event stamp.
	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[4];
	listener->delivery_stamp += 3;

	LEAVE();
}

static void
mm_event_receiver_handle_4_req(uintptr_t context UNUSED, uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	struct mm_event_fd *sink_2 = (struct mm_event_fd *) arguments[1];
	struct mm_event_fd *sink_3 = (struct mm_event_fd *) arguments[2];
	struct mm_event_fd *sink_4 = (struct mm_event_fd *) arguments[3];
	mm_event_t event_1 = arguments[4] & 0xff;
	mm_event_t event_2 = (arguments[4] >> 8) & 0xff;
	mm_event_t event_3 = (arguments[4] >> 16) & 0xff;
	mm_event_t event_4 = (arguments[4] >> 24) & 0xff;

	// Handle events.
	mm_event_handle(sink_1, event_1);
	mm_event_handle(sink_2, event_2);
	mm_event_handle(sink_3, event_3);
	mm_event_handle(sink_4, event_4);

	// Update private event stamp.
	struct mm_event_listener *listener = (struct mm_event_listener *) arguments[5];
	listener->delivery_stamp += 4;

	LEAVE();
}

static void
mm_event_receiver_batch_add(struct mm_event_listener *listener, mm_event_t event,
			    struct mm_event_fd *sink)
{
	ENTER();

	// Flush the buffer if it is full.
	if (listener->nevents == 4) {
		mm_thread_send_6(listener->thread,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) listener->events[0].ev_fd,
				 (uintptr_t) listener->events[1].ev_fd,
				 (uintptr_t) listener->events[2].ev_fd,
				 (uintptr_t) listener->events[3].ev_fd,
				 listener->events[0].event |
				 (listener->events[1].event << 8) |
				 (listener->events[2].event << 16) |
				 (listener->events[3].event << 24),
				 (uintptr_t) listener);
		listener->nevents = 0;
	}

	// Add the event to the buffer.
	unsigned int n = listener->nevents++;
	listener->events[n].event = event;
	listener->events[n].ev_fd = sink;

	// Account for the event.
	listener->forward_stamp++;

	LEAVE();
}

static void
mm_event_receiver_batch_flush(struct mm_event_listener *listener)
{
	ENTER();

	switch (listener->nevents) {
	case 1:
		mm_thread_send_3(listener->thread,
				 mm_event_receiver_handle_1_req,
				 (uintptr_t) listener->events[0].ev_fd,
				 listener->events[0].event,
				 (uintptr_t) listener);
		listener->nevents = 0;
		break;
	case 2:
		mm_thread_send_4(listener->thread,
				 mm_event_receiver_handle_2_req,
				 (uintptr_t) listener->events[0].ev_fd,
				 (uintptr_t) listener->events[1].ev_fd,
				 listener->events[0].event |
				 (listener->events[1].event << 8),
				 (uintptr_t) listener);
		listener->nevents = 0;
		break;
	case 3:
		mm_thread_send_5(listener->thread,
				 mm_event_receiver_handle_3_req,
				 (uintptr_t) listener->events[0].ev_fd,
				 (uintptr_t) listener->events[1].ev_fd,
				 (uintptr_t) listener->events[2].ev_fd,
				 listener->events[0].event |
				 (listener->events[1].event << 8) |
				 (listener->events[2].event << 16),
				 (uintptr_t) listener);
		listener->nevents = 0;
		break;
	case 4:
		mm_thread_send_6(listener->thread,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) listener->events[0].ev_fd,
				 (uintptr_t) listener->events[1].ev_fd,
				 (uintptr_t) listener->events[2].ev_fd,
				 (uintptr_t) listener->events[3].ev_fd,
				 listener->events[0].event |
				 (listener->events[1].event << 8) |
				 (listener->events[2].event << 16) |
				 (listener->events[3].event << 24),
				 (uintptr_t) listener);
		listener->nevents = 0;
		break;
	default:
		ABORT();
	}

	LEAVE();
}

void NONNULL(1, 3)
mm_event_receiver_listen(struct mm_event_receiver *receiver, mm_thread_t thread,
			 struct mm_event_backend *backend, mm_timeout_t timeout)
{
	ENTER();

	receiver->got_events = false;
	receiver->control_thread = thread;
	mm_bitset_clear_all(&receiver->targets);

	// Poll for incoming events or wait for timeout expiration.
	struct mm_event_listener *listener = &receiver->listeners[thread];
	mm_event_listener_poll(listener, backend, receiver, timeout);

	// Forward incoming events that belong to other threads.
	mm_thread_t target = mm_bitset_find(&receiver->targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_event_listener *target_listener = &receiver->listeners[target];

		// Forward incoming events.
		mm_event_receiver_batch_flush(target_listener);

		// Wake up the target thread if it is sleeping.
		mm_event_listener_notify(target_listener, backend);

		if (++target < mm_bitset_size(&receiver->targets))
			target = mm_bitset_find(&receiver->targets, target);
		else
			target = MM_THREAD_NONE;
	}

	LEAVE();
}

void NONNULL(1, 3)
mm_event_receiver_add(struct mm_event_receiver *receiver, mm_event_t event,
		      struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(receiver->control_thread == mm_thread_self());

	receiver->got_events = true;

	// Update the arrival stamp. This disables detachment of the event sink
	// until the event jumps through all the hoops and the dispatch stamp
	// is updated accordingly.
	sink->arrival_stamp++;

	if (sink->loose_target) {
		// Handle the event immediately.
		mm_event_handle(sink, event);

	} else {
		mm_thread_t target = sink->target;

		// If the event sink is detached then attach it to the control
		// thread.
		if (!sink->bound_target && target != receiver->control_thread) {
			mm_memory_load_fence();
			uint32_t dispatch_stamp = mm_memory_load(sink->dispatch_stamp);
			if (dispatch_stamp == sink->arrival_stamp) {
				target = receiver->control_thread;
				sink->target = target;
			}
		}

		// If the event sink belongs to the control thread then handle
		// it immediately, otherwise store it for later delivery to
		// the target thread.
		if (target == receiver->control_thread) {
			mm_event_handle(sink, event);
		} else {
			struct mm_event_listener *listener = &receiver->listeners[target];
			mm_event_receiver_batch_add(listener, event, sink);
			mm_bitset_set(&receiver->targets, target);
		}
	}

	LEAVE();
}
