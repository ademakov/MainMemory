/*
 * base/event/dispatch.c - MainMemory event dispatch.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#include "base/event/dispatch.h"

#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/mem/memory.h"

void __attribute__((nonnull(1)))
mm_dispatch_prepare(struct mm_dispatch *dispatch, mm_thread_t nlisteners)
{
	ENTER();
	ASSERT(nlisteners > 0);

	dispatch->lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;

	dispatch->polling_listener = MM_THREAD_NONE;

	// Allocate listener info.
	dispatch->listeners = mm_common_calloc(nlisteners,
					       sizeof(struct mm_listener));
	for (mm_thread_t i = 0; i < nlisteners; i++)
		mm_listener_prepare(&dispatch->listeners[i]);
	dispatch->nlisteners = nlisteners;

	// Initialize space for change events.
	mm_event_batch_prepare(&dispatch->changes);

	// Allocate pending event batches.
	mm_event_receiver_prepare(&dispatch->receiver, nlisteners);

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend);

	// Register the self-pipe.
	mm_event_batch_add(&dispatch->changes,
			   MM_EVENT_REGISTER,
			   &dispatch->backend.selfpipe.event_fd);
	mm_event_backend_listen(&dispatch->backend,
				&dispatch->changes,
				&dispatch->receiver.events[0],
				0);
	mm_event_batch_clear(&dispatch->changes);
	mm_event_batch_clear(&dispatch->receiver.events[0]);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_dispatch_cleanup(struct mm_dispatch *dispatch)
{
	ENTER();

	// Release pending event batches.
	mm_event_receiver_cleanup(&dispatch->receiver);

	// Release space for change events.
	mm_event_batch_cleanup(&dispatch->changes);

	// Release listener info.
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_listener_cleanup(&dispatch->listeners[i]);
	mm_common_free(dispatch->listeners);

	// Release system-specific resources.
	mm_event_backend_cleanup(&dispatch->backend);

	LEAVE();
}

static void
mm_dispatch_handle_event(struct mm_event_fd *sink, mm_event_t event)
{
	ASSERT(mm_event_target(sink) == mm_thread_getnumber(mm_thread_self()));

	// If the event sink is detached then attach it to the executing
	// thread. If it is scheduled for detachment then quit this state.
	if (!sink->attached) {
		DEBUG("attach %d to %d, stamp %u", sink->fd,
		      mm_thread_getnumber(mm_thread_self()),
		      sink->detach_stamp);

		sink->attached = 1;
		mm_event_handle(sink, MM_EVENT_ATTACH);
	} else if (sink->pending_detach) {
		sink->pending_detach = 0;
		mm_list_delete(&sink->detach_link);
	}

	// Handle the event.
	mm_event_handle(sink, event);
}

static void
mm_dispatch_handle_arrived_events(struct mm_listener *listener)
{
	// Handle arrived events.
	for (unsigned i = 0; i < listener->events.nevents; i++) {
		struct mm_event *event = &listener->events.events[i];
		if (event->event != MM_EVENT_DISPATCH_STUB)
			mm_dispatch_handle_event(event->ev_fd, event->event);
	}

	// Update event arrival stamp.
	listener->arrival_stamp = mm_event_batch_getstamp(&listener->events);

	// Forget just handled events.
	mm_event_batch_clear(&listener->events);
}

static void
mm_dispatch_handle_detach(struct mm_event_fd *sink, uint32_t stamp)
{
	ASSERT(sink->attached && sink->pending_detach);
	ASSERT(mm_event_target(sink) == mm_thread_getnumber(mm_thread_self()));
	DEBUG("detach %d from %d, stamp %u\n", sink->fd, sink->target, stamp);

	sink->pending_detach = 0;
	mm_list_delete(&sink->detach_link);

	sink->attached = 0;
	mm_event_handle(sink, MM_EVENT_DETACH);
	mm_memory_store_fence();
	mm_memory_store(sink->detach_stamp, stamp);
}

void __attribute__((nonnull(1)))
mm_dispatch_listen(struct mm_dispatch *dispatch, mm_thread_t tid,
		   mm_timeout_t timeout)
{
	ENTER();
	ASSERT(tid < dispatch->nlisteners);
	struct mm_event_receiver *receiver = &dispatch->receiver;
	struct mm_listener *listener = &dispatch->listeners[tid];
	mm_thread_t notify_listener = MM_THREAD_NONE;

	mm_regular_lock(&dispatch->lock);

	// The first arrived listener is elected to do event poll.
	if (dispatch->polling_listener == MM_THREAD_NONE) {
		// Register as a polling listener.
		dispatch->polling_listener = tid;

		// Seize all pending changes and make them private.
		mm_event_batch_append(&listener->changes, &dispatch->changes);
		mm_event_batch_clear(&dispatch->changes);

	} else if (mm_listener_has_changes(listener)) {
		notify_listener = dispatch->polling_listener;

		// Share private changes adding them to common batch.
		mm_event_batch_append(&dispatch->changes, &listener->changes);
		mm_event_batch_clear(&listener->changes);
	}

	mm_regular_unlock(&dispatch->lock);

	// Wake up a listener possibly sleeping on a poll system call.
	if (notify_listener != MM_THREAD_NONE)
		mm_dispatch_notify(dispatch, notify_listener);

	// Wait for events.
	if (dispatch->polling_listener == tid) {
		// Check to see if there are any changes that need to be
		// immediately acknowledged.
		if (mm_listener_has_urgent_changes(listener))
			timeout = 0;

		// Initialize receiver for incoming events.
		mm_event_receiver_start(receiver);

		// Wait for incoming events or timeout expiration.
		mm_listener_listen(listener, &dispatch->backend, timeout);

		// Dispatch received events.
		for (unsigned int i = 0; i < listener->events.nevents; i++) {
			struct mm_event *event = &listener->events.events[i];
			struct mm_event_fd *sink = event->ev_fd;

			mm_thread_t target = mm_memory_load(sink->target);
			mm_memory_load_fence();
			if (target != tid) {
				// If the event sink is detached attach it to
				// the current thread.
				uint32_t detach = mm_memory_load(sink->detach_stamp);
				if (detach == sink->arrival_stamp) {
					sink->target = tid;
					target = tid;
				}
			}

			sink->arrival_stamp = receiver->arrival_stamp;
			if (target == tid)
				continue;

			// The event sink is attached to another thread.
			mm_event_batch_add(&receiver->events[target],
					   event->event, sink);
			event->event = MM_EVENT_DISPATCH_STUB;

			mm_event_receiver_add_target(receiver, target);
		}

		// Forward incoming events to other threads.
		mm_thread_t target = mm_event_receiver_first_target(receiver);
		while (target != MM_THREAD_NONE) {
			struct mm_listener *tlistener = &dispatch->listeners[target];

			mm_regular_lock(&tlistener->lock);
			mm_event_batch_setstamp(&tlistener->events,
						receiver->arrival_stamp);
			mm_event_batch_append(&tlistener->events,
					      &receiver->events[target]);
			mm_regular_unlock(&tlistener->lock);

			mm_dispatch_notify(dispatch, target);
			mm_event_batch_clear(&receiver->events[target]);
			target = mm_event_receiver_next_target(receiver, target);
		}

		// Handle incoming events.
		mm_event_batch_setstamp(&listener->events,
					receiver->arrival_stamp);
		mm_dispatch_handle_arrived_events(listener);

		// Unregister the listener from poll.
		mm_regular_lock(&dispatch->lock);
		dispatch->polling_listener = MM_THREAD_NONE;
		mm_regular_unlock(&dispatch->lock);

		// Forget just handled changes.
		mm_event_batch_clear(&listener->changes);

		// TODO: possibly wake any waiting listener if there are
		// pending changes.
		// if (!mm_event_batch_empty(&dispatch->pending_changes)) ...

	} else {
		// Wait for a notification or timeout expiration.
		mm_listener_listen(listener, NULL, timeout);

		// Handle received events.
		mm_regular_lock(&listener->lock);
		mm_dispatch_handle_arrived_events(listener);
		mm_regular_unlock(&listener->lock);
	}

	// Finalize remaining detach requests.
	while (!mm_list_empty(&listener->detach_list)) {
		struct mm_link *link = mm_list_head(&listener->detach_list);
		struct mm_event_fd *sink = containerof(link, struct mm_event_fd, detach_link);
		mm_dispatch_handle_detach(sink, listener->arrival_stamp);
	}

	LEAVE();
}
