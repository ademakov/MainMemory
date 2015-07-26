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

#include "base/event/batch.h"
#include "base/log/debug.h"
#include "base/log/trace.h"
#include "base/mem/memory.h"

void __attribute__((nonnull(1)))
mm_event_receiver_prepare(struct mm_event_receiver *receiver,
			  mm_thread_t ntargets)
{
	ENTER();

	receiver->arrival_stamp = 0;

	receiver->nlisteners = ntargets;

	// Allocate listener info.
	receiver->listeners = mm_common_calloc(ntargets,
					       sizeof(struct mm_listener));
	for (mm_thread_t i = 0; i < ntargets; i++)
		mm_listener_prepare(&receiver->listeners[i]);

	receiver->events = mm_common_calloc(ntargets,
					    sizeof(struct mm_event_batch));
	for (mm_thread_t i = 0; i < ntargets; i++)
		mm_event_batch_prepare(&receiver->events[i]);

	mm_bitset_prepare(&receiver->targets, &mm_common_space.xarena,
			  ntargets);

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

	for (mm_thread_t i = 0; i < receiver->nlisteners; i++)
		mm_event_batch_cleanup(&receiver->events[i]);

	mm_bitset_cleanup(&receiver->targets, &mm_common_space.xarena);

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

	// Update the private arrival stamp.
	listener->arrival_stamp = receiver->arrival_stamp;

	// Forward incoming events that belong to other threads.
	mm_thread_t target = mm_bitset_find(&receiver->targets, 0);
	while (target != MM_THREAD_NONE) {
		struct mm_listener *target_listener = &receiver->listeners[target];

		// Forward incoming events.
		mm_regular_lock(&target_listener->lock);
		mm_event_batch_append(&target_listener->events,
				      &receiver->events[target]);
		target_listener->arrival_stamp = receiver->arrival_stamp;
		mm_regular_unlock(&target_listener->lock);

		// Wake up the target thread if it is sleeping.
		mm_listener_notify(target_listener, backend);

		// Forget just forwarded events.
		mm_event_batch_clear(&receiver->events[target]);

		if (++target < mm_bitset_size(&receiver->targets))
			target = mm_bitset_find(&receiver->targets, target);
		else
			break;
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
	sink->arrival_stamp = receiver->arrival_stamp;

	// If the event sink belongs to the control thread then handle it
	// immediately, otherwise store it for later delivery to the target
	// thread.
	if (target == receiver->control_thread) {
		mm_event_handle(sink, event);
	} else {
		mm_event_batch_add(&receiver->events[target],
				   event, sink);
		mm_bitset_set(&receiver->targets, target);
	}

	LEAVE();
}
