/*
 * event/dispatch.c - MainMemory event dispatch.
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

#include "event/dispatch.h"

#include "core/core.h"

#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/mem/space.h"

void __attribute__((nonnull(1)))
mm_dispatch_prepare(struct mm_dispatch *dispatch)
{
	ENTER();

	mm_core_t ncores = mm_core_getnum();
	ASSERT(ncores > 0);

	dispatch->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;

	dispatch->polling_listener = NULL;
	dispatch->waiting_listeners = mm_common_calloc(ncores, sizeof (struct mm_listener *));

	// Allocate pending event batches.
	dispatch->pending_events = mm_common_alloc(ncores * sizeof(struct mm_event_batch));
	for (mm_core_t i = 0; i < ncores; i++)
		mm_event_batch_prepare(&dispatch->pending_events[i]);
	mm_event_batch_prepare(&dispatch->pending_changes);

	// Initialize system-specific resources.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_prepare(&dispatch->events);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_prepare(&dispatch->events);
#endif

	// Open a self-pipe.
	mm_selfpipe_prepare(&dispatch->selfpipe);

	// Register the self-pipe.
	mm_event_batch_add(&dispatch->pending_changes,
			   MM_EVENT_REGISTER,
			   &dispatch->selfpipe.event_fd);
	mm_dispatch_listen(dispatch,
			   &dispatch->pending_changes,
			   &dispatch->pending_changes,
			   0);
	mm_event_batch_clear(&dispatch->pending_changes);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_dispatch_cleanup(struct mm_dispatch *dispatch)
{
	ENTER();

	mm_core_t ncores = mm_core_getnum();

	mm_common_free(dispatch->waiting_listeners);

	// Release pending event batches.
	for (mm_core_t i = 0; i < ncores; i++)
		mm_event_batch_cleanup(&dispatch->pending_events[i]);
	mm_event_batch_cleanup(&dispatch->pending_changes);

	// Close the event self-pipe.
	mm_selfpipe_cleanup(&dispatch->selfpipe);

	// Close the epoll/kqueue file descriptor.
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_cleanup(&dispatch->events);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_cleanup(&dispatch->events);
#endif

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_dispatch_checkin(struct mm_dispatch *dispatch, struct mm_listener *listener)
{
	ENTER();

	mm_core_t core = mm_core_selfid();
	struct mm_listener *polling_listener = NULL;

	mm_task_lock(&dispatch->lock);

	// Get pending incoming events if any.
	mm_event_batch_append(&listener->events, &dispatch->pending_events[core]);
	mm_event_batch_clear(&dispatch->pending_events[core]);

	// The first arrived listener is elected to do event poll.
	if (dispatch->polling_listener == NULL) {
		// Register as a polling listener.
		dispatch->polling_listener = listener;

		// Seize all pending changes and make them private.
		mm_event_batch_append(&listener->changes, &dispatch->pending_changes);
		mm_event_batch_clear(&dispatch->pending_changes);
	} else {
		// Register as a waiting listener.
		dispatch->waiting_listeners[core] = listener;

		// Make private changes public adding them to pending changes.
		if (mm_listener_has_changes(listener)) {
			mm_event_batch_append(&dispatch->pending_changes, &listener->changes);
			polling_listener = dispatch->polling_listener;
		}
	}

	mm_task_unlock(&dispatch->lock);

	// Wake up a listener that is possibly sleeping in a poll system call.
	if (polling_listener != NULL)
		mm_listener_notify(polling_listener, dispatch);
 
	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_dispatch_checkout(struct mm_dispatch *dispatch, struct mm_listener *listener)
{
	ENTER();

	mm_core_t core = mm_core_selfid();

	if (dispatch->polling_listener == listener) {
		unsigned int nevents = 0, nlisteners = 0;

		mm_task_lock(&dispatch->lock);

		// Unregister as polling listener.
		dispatch->polling_listener = NULL;

		// Dispatch received events.
		for (unsigned int i = 0; i < listener->events.nevents; i++) {
			struct mm_event *event = &listener->events.events[i];

			mm_core_t ev_core = event->ev_fd->core;
			if (ev_core == core)
				continue;
			if (ev_core == MM_CORE_SELF)
				continue;
			if (ev_core == MM_CORE_NONE)
				continue;
			ASSERT(ev_core < mm_core_getnum());

			mm_event_batch_add(&dispatch->pending_events[ev_core],
					   event->event, event->ev_fd);
			event->event = MM_EVENT_DISPATCH_STUB;
			nevents++;

			struct mm_listener *target = dispatch->waiting_listeners[ev_core];
			if (target != NULL) {
				dispatch->waiting_listeners[ev_core] = NULL;

				ASSERT(nlisteners < mm_core_getnum());
				listener->dispatch_targets[nlisteners++] = target;
			}
		}

		// TODO: possibly wake any waiting listener if there are
		// pending changes.
		// if (!mm_event_batch_empty(&dispatch->pending_changes)) ...

		mm_task_unlock(&dispatch->lock);

		for (unsigned int i = 0; i < nlisteners; i++) {
			struct mm_listener *target = listener->dispatch_targets[i];
			mm_listener_notify(target, dispatch);
		}

	} else {
		mm_task_lock(&dispatch->lock);

		// Unregister as waiting listener.
		dispatch->waiting_listeners[core] = NULL;

		// Get pending incoming events if any.
		mm_event_batch_append(&listener->events, &dispatch->pending_events[core]);
		mm_event_batch_clear(&dispatch->pending_events[core]);

		mm_task_unlock(&dispatch->lock);
	}

	LEAVE();
}
