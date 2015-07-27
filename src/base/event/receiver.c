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
#include "base/mem/memory.h"
#include "base/thread/thread.h"

void __attribute__((nonnull(1, 3)))
mm_event_receiver_prepare(struct mm_event_receiver *receiver,
			  mm_thread_t nthreads,
			  struct mm_thread *threads[])
{
	ENTER();

	receiver->arrival_stamp = 0;

	// Allocate listener info.
	receiver->nlisteners = nthreads;
	receiver->listeners = mm_common_calloc(nthreads,
					       sizeof(struct mm_listener));
	for (mm_thread_t i = 0; i < nthreads; i++)
		mm_listener_prepare(&receiver->listeners[i], threads[i]);

	mm_bitset_prepare(&receiver->targets, &mm_common_space.xarena,
			  nthreads);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_receiver_cleanup(struct mm_event_receiver *receiver)
{
	ENTER();

	// Release listener info.
	for (mm_thread_t i = 0; i < receiver->nlisteners; i++)
		mm_listener_cleanup(&receiver->listeners[i]);
	mm_common_free(receiver->listeners);

	mm_bitset_cleanup(&receiver->targets, &mm_common_space.xarena);

	LEAVE();
}

static void
mm_event_receiver_handle_0_req(uintptr_t context __mm_unused__,
			       uintptr_t *arguments)
{
	ENTER();

	// Update private event stamp.
	struct mm_listener *listener = (struct mm_listener *) arguments[0];
	listener->handle_stamp = arguments[1];

	LEAVE();
}

static void
mm_event_receiver_handle_1_req(uintptr_t context __mm_unused__,
			       uintptr_t *arguments)
{
	ENTER();

	struct mm_event_fd *sink_1 = (struct mm_event_fd *) arguments[0];
	mm_event_t event_1 = arguments[1];

	// Handle events.
	mm_event_handle(sink_1, event_1);

	// Update private event stamp.
	struct mm_listener *listener = (struct mm_listener *) arguments[2];
	listener->handle_stamp = arguments[3];

	LEAVE();
}

static void
mm_event_receiver_handle_2_req(uintptr_t context __mm_unused__,
			       uintptr_t *arguments)
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
	struct mm_listener *listener = (struct mm_listener *) arguments[3];
	listener->handle_stamp = arguments[4];

	LEAVE();
}

static void
mm_event_receiver_handle_3_req(uintptr_t context __mm_unused__,
			       uintptr_t *arguments)
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
	struct mm_listener *listener = (struct mm_listener *) arguments[4];
	listener->handle_stamp = arguments[5];

	LEAVE();
}

static void
mm_event_receiver_handle_4_req(uintptr_t context __mm_unused__,
			       uintptr_t *arguments)
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

	LEAVE();
}

static void
mm_event_receiver_batch_add(struct mm_listener *listener,
			    mm_event_t event, struct mm_event_fd *sink)
{
	ENTER();

	struct mm_event_batch *batch = &listener->events;
	if (batch->nevents == 4) {
		mm_thread_send_5(listener->thread,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) batch->events[0].ev_fd,
				 (uintptr_t) batch->events[1].ev_fd,
				 (uintptr_t) batch->events[2].ev_fd,
				 (uintptr_t) batch->events[3].ev_fd,
				 batch->events[0].event |
				 (batch->events[1].event << 8) |
				 (batch->events[2].event << 16) |
				 (batch->events[3].event << 24));
		mm_event_batch_clear(batch);
	}

	mm_event_batch_add(batch, event, sink);

	LEAVE();
}

static void
mm_event_receiver_batch_flush(struct mm_listener *listener,
			      uint32_t stamp)
{
	ENTER();

	struct mm_event_batch *batch = &listener->events;
	switch (batch->nevents) {
	case 1:
		mm_thread_send_4(listener->thread,
				 mm_event_receiver_handle_1_req,
				 (uintptr_t) batch->events[0].ev_fd,
				 batch->events[0].event,
				 (uintptr_t) listener,
				 stamp);
		mm_event_batch_clear(batch);
		break;
	case 2:
		mm_thread_send_5(listener->thread,
				 mm_event_receiver_handle_2_req,
				 (uintptr_t) batch->events[0].ev_fd,
				 (uintptr_t) batch->events[1].ev_fd,
				 batch->events[0].event |
				 (batch->events[1].event << 8),
				 (uintptr_t) listener,
				 stamp);
		mm_event_batch_clear(batch);
		break;
	case 3:
		mm_thread_send_6(listener->thread,
				 mm_event_receiver_handle_3_req,
				 (uintptr_t) batch->events[0].ev_fd,
				 (uintptr_t) batch->events[1].ev_fd,
				 (uintptr_t) batch->events[2].ev_fd,
				 batch->events[0].event |
				 (batch->events[1].event << 8) |
				 (batch->events[2].event << 16),
				 (uintptr_t) listener,
				 stamp);
		mm_event_batch_clear(batch);
		break;
	case 4:
		mm_thread_send_5(listener->thread,
				 mm_event_receiver_handle_4_req,
				 (uintptr_t) batch->events[0].ev_fd,
				 (uintptr_t) batch->events[1].ev_fd,
				 (uintptr_t) batch->events[2].ev_fd,
				 (uintptr_t) batch->events[3].ev_fd,
				 batch->events[0].event |
				 (batch->events[1].event << 8) |
				 (batch->events[2].event << 16) |
				 (batch->events[3].event << 24));
		mm_thread_send_2(listener->thread,
				 mm_event_receiver_handle_0_req,
				 (uintptr_t) listener,
				 stamp);
		mm_event_batch_clear(batch);
		break;
	default:
		ABORT();
	}

	LEAVE();
}

void __attribute__((nonnull(1, 3)))
mm_event_receiver_listen(struct mm_event_receiver *receiver,
			 mm_thread_t thread,
			 struct mm_event_backend *backend,
			 mm_timeout_t timeout)
{
	ENTER();

	receiver->control_thread = thread;
	mm_bitset_clear_all(&receiver->targets);

	// Poll for incoming events or wait for timeout expiration.
	struct mm_listener *listener = &receiver->listeners[thread];
	mm_listener_poll(listener, backend, receiver, timeout);

	// Update private event stamp.
	listener->handle_stamp = listener->arrival_stamp;

	// Forward incoming events that belong to other threads.
	mm_thread_t target = mm_bitset_find(&receiver->targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_listener *target_listener = &receiver->listeners[target];

		// Forward incoming events.
		mm_event_receiver_batch_flush(target_listener,
					      receiver->arrival_stamp);

		// Wake up the target thread if it is sleeping.
		mm_listener_notify(target_listener, backend);

		if (++target < mm_bitset_size(&receiver->targets))
			target = mm_bitset_find(&receiver->targets, target);
		else
			target = MM_THREAD_NONE;
	}

	LEAVE();
}

void __attribute__((nonnull(1, 3)))
mm_event_receiver_add(struct mm_event_receiver *receiver,
		      mm_event_t event, struct mm_event_fd *sink)
{
	ENTER();
	ASSERT(receiver->control_thread == mm_thread_self());

	mm_thread_t target = mm_memory_load(sink->target);
	mm_memory_load_fence();

	// If the event sink is detached then attach it to the control thread.
	if (target != receiver->control_thread) {
		uint32_t detach_stamp = mm_memory_load(sink->detach_stamp);
		if (detach_stamp == sink->arrival_stamp) {
			sink->target = receiver->control_thread;
			target = receiver->control_thread;
		}
	}

	// Update the arrival stamp. This disables detachment of the event
	// sink until the received event jumps through all the hoops and
	// the detach stamp is updated accordingly.
	struct mm_listener *listener = &receiver->listeners[target];
	listener->arrival_stamp = receiver->arrival_stamp;
	sink->arrival_stamp = receiver->arrival_stamp;

	// If the event sink belongs to the control thread then handle it
	// immediately, otherwise store it for later delivery to the target
	// thread.
	if (target == receiver->control_thread) {
		mm_event_handle(sink, event);
	} else {
		mm_event_receiver_batch_add(listener, event, sink);
		mm_bitset_set(&receiver->targets, target);
	}

	LEAVE();
}
