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

	dispatch->control_thread = MM_THREAD_NONE;
#if ENABLE_DEBUG
	dispatch->last_control_thread = MM_THREAD_NONE;
#endif

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

	// Update private event handling stamp.
	listener->handle_stamp = listener->arrival_stamp;

	// Forget just handled events.
	mm_event_batch_clear(&listener->events);
}

static void
mm_dispatch_handle_detach(struct mm_listener *listener)
{
	while (!mm_list_empty(&listener->detach_list)) {
		struct mm_link *link = mm_list_head(&listener->detach_list);
		struct mm_event_fd *sink
			= containerof(link, struct mm_event_fd, detach_link);
		mm_event_detach(sink, listener->handle_stamp);
	}
}

void __attribute__((nonnull(1)))
mm_dispatch_listen(struct mm_dispatch *dispatch, mm_thread_t thread,
		   mm_timeout_t timeout)
{
	ENTER();
	ASSERT(thread < dispatch->nlisteners);
	struct mm_event_receiver *receiver = &dispatch->receiver;
	struct mm_listener *listener = &dispatch->listeners[thread];

	// Handle the change events.
	mm_regular_lock(&dispatch->lock);
	mm_thread_t control_thread = dispatch->control_thread;
	if (control_thread == MM_THREAD_NONE) {

#if ENABLE_DEBUG
		if (dispatch->last_control_thread != thread) {
			DEBUG("switch control thread %d -> %d",
			      dispatch->last_control_thread, thread);
			dispatch->last_control_thread = thread;
		}
#endif

		// The first arrived thread is elected to conduct the next
		// event poll.
		dispatch->control_thread = thread;

		// Capture all the published change events.
		mm_event_batch_append(&listener->changes, &dispatch->changes);
		// Forget just captured events.
		mm_event_batch_clear(&dispatch->changes);

		// Setup the event receiver for the next poll cycle.
		mm_event_receiver_start(receiver, thread);

		mm_regular_unlock(&dispatch->lock);

		// If this thread previously published any change events
		// then upon this dispatch cycle the events will certainly
		// be handled (perhaps by the very same thread).
		listener->changes_state = MM_LISTENER_CHANGES_PRIVATE;

	} else if (mm_listener_has_changes(listener)) {

		// Publish the private change events.
		mm_event_batch_append(&dispatch->changes, &listener->changes);

		// The published events might be missed by the current
		// control thread. So the publisher must force another
		// dispatch cycle.
		listener->changes_state = MM_LISTENER_CHANGES_PUBLISHED;
		listener->changes_stamp = receiver->arrival_stamp;

		mm_regular_unlock(&dispatch->lock);

		// Forget just published events.
		mm_event_batch_clear(&listener->changes);

		// Wake up the control thread if it is still sleeping.
		// But by this time the known control thread might have
		// given up its role and be busy with something else.
		// So it might be needed to wake up a new control thread.
		mm_dispatch_notify(dispatch, control_thread);
		// Avoid sleeping until another dispatch cycle begins.
		timeout = 0;

	} else {
		mm_regular_unlock(&dispatch->lock);

		if (listener->changes_state != MM_LISTENER_CHANGES_PRIVATE) {
			uint32_t stamp = mm_memory_load(receiver->arrival_stamp);
			if (listener->changes_stamp != stamp) {
				// At this point the change events published by this
				// thread must have been captured by a control thread.
				listener->changes_state = MM_LISTENER_CHANGES_PRIVATE;
			} else {
				// Wake up the control thread if it is still sleeping.
				mm_dispatch_notify(dispatch, control_thread);
				// Avoid sleeping until another dispatch cycle begins.
				timeout = 0;
			}
		}
	}

	// Wait for incoming events.
	if (control_thread == MM_THREAD_NONE) {
		// Check to see if there are any changes that need to be
		// immediately acknowledged.
		if (mm_listener_has_urgent_changes(listener))
			timeout = 0;

		// Handle events that might have been forwarded to this
		// listener by a previous control thread. There is no need
		// to lock the listener here as only the control thread and
		// the listener owner thread could ever access the events
		// concurrently. And both of these threads are the same one
		// at this point.
		if (mm_listener_has_events(listener)) {
			mm_dispatch_handle_events(listener);
			timeout = 0;
		}

		// Poll for incoming events or wait for timeout expiration.
		mm_listener_listen(listener,
				   &dispatch->backend, &dispatch->receiver,
				   timeout);

		// Update private event arrival stamp.
		listener->arrival_stamp = receiver->arrival_stamp;
		listener->handle_stamp = receiver->arrival_stamp;

		// Forward incoming events that belong to other threads.
		mm_thread_t target = mm_event_receiver_first_target(receiver);
		while (target != MM_THREAD_NONE) {
			struct mm_listener *target_listener
				= &dispatch->listeners[target];

			// Forward incoming events.
			mm_regular_lock(&target_listener->lock);
			mm_event_batch_append(&target_listener->events,
					      &receiver->events[target]);
			listener->arrival_stamp = receiver->arrival_stamp;
			mm_regular_unlock(&target_listener->lock);

			// Wake up the target thread if it is sleeping.
			mm_dispatch_notify(dispatch, target);

			// Forget just forwarded events.
			mm_event_batch_clear(&receiver->events[target]);

			target = mm_event_receiver_next_target(receiver, target);
		}

		// Give up the control thread role.
		mm_memory_store_fence();
		mm_memory_store(dispatch->control_thread, MM_THREAD_NONE);

		// Forget just handled change events.
		mm_event_batch_clear(&listener->changes);

	} else {
		// Wait for forwarded events or timeout expiration.
		mm_listener_listen(listener, NULL, NULL, timeout);

		// Handle the forwarded events.
		mm_regular_lock(&listener->lock);
		mm_dispatch_handle_events(listener);
		mm_regular_unlock(&listener->lock);

		// Finalize the detach requests.
		mm_dispatch_handle_detach(listener);
	}

	LEAVE();
}
