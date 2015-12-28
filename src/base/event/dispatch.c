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
#include "base/memory/memory.h"

void NONNULL(1, 3)
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
	dispatch->publish_stamp = 0;

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend);

	// Allocate pending event batches.
	mm_event_receiver_prepare(&dispatch->receiver, nthreads, threads);

	// Determine event flags that require change event serialization.
	if (mm_event_backend_serial(&dispatch->backend)) {
		// The backend requires all changes to be serialized.
		dispatch->serial_changes = (MM_EVENT_BATCH_REGISTER
					    | MM_EVENT_BATCH_UNREGISTER
					    | MM_EVENT_BATCH_INPUT_OUTPUT);
	} else {
		// The backend has no requirements. However we want to
		// ensure that event sinks get no other events after
		// unregistering.
		dispatch->serial_changes = MM_EVENT_BATCH_UNREGISTER;
	}

	LEAVE();
}

void NONNULL(1)
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

static inline bool NONNULL(1)
mm_dispatch_has_urgent_changes(struct mm_event_listener *listener)
{
	return mm_event_listener_hasflags(listener, MM_EVENT_BATCH_UNREGISTER);
}

void NONNULL(1)
mm_dispatch_listen(struct mm_dispatch *dispatch, mm_thread_t thread, mm_timeout_t timeout)
{
	ENTER();
	ASSERT(thread < dispatch->receiver.nlisteners);
	struct mm_event_listener *listener = mm_dispatch_listener(dispatch, thread);

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
		// Start the next publish/capture round.
		dispatch->publish_stamp++;

		mm_regular_unlock(&dispatch->lock);

		// If this thread previously published any change events
		// then upon this dispatch cycle the events will certainly
		// be handled (perhaps by the very same thread).
		listener->changes_state = MM_EVENT_LISTENER_CHANGES_PRIVATE;

	} else if (mm_event_listener_has_changes(listener)) {

		// Publish the private change events.
		mm_event_batch_append(&dispatch->changes, &listener->changes);

		// The changes just published might be missed by the current
		// control thread. So the publisher must force another
		// dispatch cycle.
		listener->changes_state = MM_EVENT_LISTENER_CHANGES_PUBLISHED;
		listener->publish_stamp = dispatch->publish_stamp;

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

		if (listener->changes_state != MM_EVENT_LISTENER_CHANGES_PRIVATE) {
			if (listener->publish_stamp != dispatch->publish_stamp) {
				// At this point the change events published by this
				// thread must have been captured by a control thread.
				listener->changes_state = MM_EVENT_LISTENER_CHANGES_PRIVATE;
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
		if (mm_dispatch_has_urgent_changes(listener)) {
			// There are changes that need to be immediately
			// acknowledged.
			timeout = 0;
		}

		// Wait for incoming events or timeout expiration.
		mm_event_receiver_listen(&dispatch->receiver, &dispatch->backend, thread, timeout);

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
		// Wait for forwarded events or timeout expiration.
		mm_event_listener_wait(listener, timeout);
	}

	LEAVE();
}
