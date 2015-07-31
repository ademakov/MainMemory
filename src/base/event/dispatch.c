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

void __attribute__((nonnull(1, 3)))
mm_dispatch_prepare(struct mm_dispatch *dispatch,
		    mm_thread_t nthreads,
		    struct mm_thread *threads[])
{
	ENTER();
	ASSERT(nthreads > 0);

	dispatch->lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;

	dispatch->control_thread = MM_THREAD_NONE;
#if ENABLE_DEBUG
	dispatch->last_control_thread = MM_THREAD_NONE;
#endif
#if ENABLE_DISPATCH_BUSYWAIT
	dispatch->busywait = 0;
#endif

	// Initialize space for change events.
	mm_event_batch_prepare(&dispatch->changes, 1024);

	// Allocate pending event batches.
	mm_event_receiver_prepare(&dispatch->receiver, nthreads, threads);

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend);

	// Register the self-pipe.
	mm_event_batch_add(&dispatch->changes,
			   MM_EVENT_REGISTER,
			   &dispatch->backend.selfpipe.event_fd);
	mm_event_receiver_start(&dispatch->receiver);
	mm_event_backend_listen(&dispatch->backend,
				&dispatch->changes,
				&dispatch->receiver,
				0);
	mm_event_batch_clear(&dispatch->changes);

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

	// Release system-specific resources.
	mm_event_backend_cleanup(&dispatch->backend);

	LEAVE();
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
	ASSERT(thread < dispatch->receiver.nlisteners);
	struct mm_event_receiver *receiver = &dispatch->receiver;
	struct mm_listener *listener = mm_dispatch_listener(dispatch, thread);

	// Handle the change events.
	mm_regular_lock(&dispatch->lock);
	mm_thread_t control_thread = dispatch->control_thread;
	if (control_thread == MM_THREAD_NONE) {

		// About to become a control thread check to see if there
		// are any unhandled events forwarded by a previous control
		// thread. If this is so then bail out in order to handle
		// them. This is to preserve event arrival order.
		if (listener->arrival_stamp != listener->handle_stamp) {
			DEBUG("arrival stamp %d, handle_stamp %d",
			      listener->arrival_stamp,
			      listener->handle_stamp);
			mm_regular_unlock(&dispatch->lock);
			goto leave;
		}

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
		mm_event_receiver_start(receiver);

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
#if ENABLE_DISPATCH_BUSYWAIT
		if (dispatch->busywait) {
			// Presume that if there were incoming events
			// moments ago then there is a chance to get
			// some more immediately. Spin a little bit to
			// avoid context switches.
			dispatch->busywait--;
			timeout = 0;
		}
		else
#endif
		if (mm_listener_has_urgent_changes(listener)) {
			// There are changes that need to be immediately
			// acknowledged.
			timeout = 0;
		}

		// Wait for incoming events or timeout expiration.
		mm_event_receiver_listen(&dispatch->receiver, thread,
					 &dispatch->backend, timeout);

#if ENABLE_DISPATCH_BUSYWAIT
		if (dispatch->receiver.got_events)
			dispatch->busywait = 250;
#endif

		// Give up the control thread role.
		mm_memory_store_fence();
		mm_memory_store(dispatch->control_thread, MM_THREAD_NONE);

		// Forget just handled change events.
		mm_event_batch_clear(&listener->changes);

	} else {
		// Finalize the detach requests.
		mm_dispatch_handle_detach(listener);

		// Wait for forwarded events or timeout expiration.
		mm_listener_wait(listener, timeout);
	}

leave:
	LEAVE();
}

void __attribute__((nonnull(1)))
mm_dispatch_notify_waiting(struct mm_dispatch *dispatch)
{
	ENTER();

	mm_thread_t n = dispatch->receiver.nlisteners;
	for (mm_thread_t i = 0; i < n; i++) {
		struct mm_listener *listener = mm_dispatch_listener(dispatch, i);
		if (mm_memory_load(listener->state) == MM_LISTENER_WAITING) {
			mm_listener_notify(listener, &dispatch->backend);
			break;
		}
	}

	LEAVE();
}
