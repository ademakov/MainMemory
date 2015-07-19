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
	mm_event_receiver_start(&dispatch->receiver, 0);
	mm_event_backend_listen(&dispatch->backend,
				&dispatch->changes,
				&dispatch->receiver,
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
mm_dispatch_handle_events(struct mm_listener *listener)
{
	// Handle arrived events.
	for (unsigned i = 0; i < listener->events.nevents; i++) {
		struct mm_event *event = &listener->events.events[i];
		mm_event_handle(event->ev_fd, event->event);
	}

	// Update event arrival stamp.
	listener->arrival_stamp = mm_event_batch_getstamp(&listener->events);

	// Forget just handled events.
	mm_event_batch_clear(&listener->events);
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

		// Handle received events.
		if (mm_listener_has_events(listener)) {
			mm_dispatch_handle_events(listener);
			timeout = 0;
		}

		// Initialize receiver for incoming events.
		mm_event_receiver_start(receiver, tid);

		// Wait for incoming events or timeout expiration.
		mm_listener_listen(listener,
				   &dispatch->backend, &dispatch->receiver,
				   timeout);

		// Forward incoming events that belong to other threads.
		mm_thread_t target = mm_event_receiver_first_target(receiver);
		while (target != MM_THREAD_NONE) {
			struct mm_listener *tlistener = &dispatch->listeners[target];

			// Forward incoming events.
			mm_regular_lock(&tlistener->lock);
			mm_event_batch_setstamp(&tlistener->events,
						receiver->arrival_stamp);
			mm_event_batch_append(&tlistener->events,
					      &receiver->events[target]);
			mm_regular_unlock(&tlistener->lock);

			// Wake the target thread if it is sleeping.
			mm_dispatch_notify(dispatch, target);

			// Forget just forwarded events.
			mm_event_batch_clear(&receiver->events[target]);

			target = mm_event_receiver_next_target(receiver, target);
		}

		// Unregister the listener from poll.
		mm_regular_lock(&dispatch->lock);
		dispatch->polling_listener = MM_THREAD_NONE;
		mm_regular_unlock(&dispatch->lock);

		// Forget just handled changes.
		mm_event_batch_clear(&listener->changes);

		// TODO: possibly wake any waiting listener if there are
		// pending changes.
		// if (!mm_event_batch_empty(&dispatch->changes)) ...

	} else {
		// Wait for a notification or timeout expiration.
		mm_listener_listen(listener, NULL, NULL, timeout);

		// Handle received events.
		mm_regular_lock(&listener->lock);
		mm_dispatch_handle_events(listener);
		mm_regular_unlock(&listener->lock);
	}

	// Finalize remaining detach requests.
	while (!mm_list_empty(&listener->detach_list)) {
		struct mm_link *link = mm_list_head(&listener->detach_list);
		struct mm_event_fd *sink = containerof(link, struct mm_event_fd, detach_link);
		mm_event_detach(sink, listener->arrival_stamp);
	}

	LEAVE();
}
