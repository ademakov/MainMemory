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

	dispatch->lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;

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
			   &dispatch->pending_events[0],
			   0);
	mm_event_batch_clear(&dispatch->pending_changes);
	mm_event_batch_clear(&dispatch->pending_events[0]);

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

static void
mm_dispatch_check_events(struct mm_dispatch *dispatch,
			 struct mm_listener *listener,
			 mm_core_t core)
{
	// Prepare to detach finished event sinks.
	struct mm_event_batch *finish_events = &listener->finish;
	for (unsigned int i = 0; i < finish_events->nevents; i++) {
		struct mm_event *event = &finish_events->events[i];
		struct mm_event_fd *ev_fd = event->ev_fd;
		ev_fd->target = MM_THREAD_NONE;
		ev_fd->detach = core;
	}

	struct mm_event_batch *pending_events = &dispatch->pending_events[core];
	if (mm_event_batch_empty(pending_events))
		return;

	// Check if pending events affect any of the finished events.
	// Undo detach preparation in this case.
	for (unsigned int i = 0; i < pending_events->nevents; i++) {
		struct mm_event *event = &pending_events->events[i];
		struct mm_event_fd *ev_fd = event->ev_fd;
		if (ev_fd->detach != MM_THREAD_NONE) {
			ev_fd->detach = MM_THREAD_NONE;
			ev_fd->target = core;
		}
	}

	// Grab pending incoming events.
	mm_event_batch_append(&listener->events, pending_events);
	mm_event_batch_clear(pending_events);
}

static void
mm_dispatch_get_pending_events(struct mm_dispatch *dispatch,
			       struct mm_listener *listener,
			       mm_core_t core)
{
	struct mm_event_batch *pending_events = &dispatch->pending_events[core];
	if (mm_event_batch_empty(pending_events))
		return;

	// Grab pending incoming events.
	mm_event_batch_append(&listener->events, pending_events);
	mm_event_batch_clear(pending_events);
}

static void
mm_dispatch_detach(struct mm_listener *listener)
{
	ENTER();

	struct mm_event_batch *finish_events = &listener->finish;
	for (unsigned int i = 0; i < finish_events->nevents; i++) {
		struct mm_event *event = &finish_events->events[i];
		struct mm_event_fd *ev_fd = event->ev_fd;
		if (ev_fd->detach != MM_THREAD_NONE) {
			ASSERT(ev_fd->target == MM_THREAD_NONE);
			mm_event_dispatch(ev_fd, MM_EVENT_DETACH);
			mm_memory_store_fence();
			mm_memory_store(ev_fd->detach, MM_THREAD_NONE);
		}
	}

	mm_event_batch_clear(finish_events);

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_dispatch_checkin(struct mm_dispatch *dispatch, struct mm_listener *listener)
{
	ENTER();

	mm_core_t core = mm_core_selfid();

	mm_regular_lock(&dispatch->lock);

	// The first arrived listener is elected to do event poll.
	if (dispatch->polling_listener == NULL) {
		// Register as a polling listener.
		dispatch->polling_listener = listener;

		// Seize all pending changes and make them private.
		mm_event_batch_append(&listener->changes, &dispatch->pending_changes);
		mm_event_batch_clear(&dispatch->pending_changes);

		mm_regular_unlock(&dispatch->lock);

		// Get pending incoming events and prepare detach events.
		mm_dispatch_check_events(dispatch, listener, core);

	} else {
		// Register as a waiting listener.
		dispatch->waiting_listeners[core] = listener;

		// Make private changes public adding them to pending changes.
		struct mm_listener *notify_listener = NULL;
		if (mm_listener_has_changes(listener)) {
			mm_event_batch_append(&dispatch->pending_changes, &listener->changes);
			notify_listener = dispatch->polling_listener;
		}

		// Get pending incoming events and prepare detach events.
		mm_dispatch_check_events(dispatch, listener, core);

		mm_regular_unlock(&dispatch->lock);

		// Finalize detach events.
		mm_dispatch_detach(listener);

		// Wake up a listener that is possibly sleeping on a poll system call.
		if (notify_listener != NULL)
			mm_listener_notify(notify_listener, dispatch);
	}

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_dispatch_checkout(struct mm_dispatch *dispatch, struct mm_listener *listener)
{
	ENTER();

	mm_core_t core = mm_core_selfid();

	if (dispatch->polling_listener == listener) {
		unsigned int nlisteners = 0;

		mm_regular_lock(&dispatch->lock);

		// Unregister as polling listener.
		dispatch->polling_listener = NULL;

		// Dispatch received events.
		for (unsigned int i = 0; i < listener->events.nevents; i++) {
			struct mm_event *event = &listener->events.events[i];
			struct mm_event_fd *ev_fd = event->ev_fd;

			// Check to see if the event sink is attached to this
			// thread.
			mm_thread_t target = ev_fd->target;
			if (target == core)
				continue;

			// Check to see if the event sink is detached. In this
			// case attach it to itself.
			if (target == MM_THREAD_NONE) {
				ev_fd->target = core;
				continue;
			}

			// The event sink is attached to another thread.
			mm_event_batch_add(&dispatch->pending_events[target],
					   event->event, ev_fd);
			event->event = MM_EVENT_DISPATCH_STUB;

			struct mm_listener *target_listener
				= dispatch->waiting_listeners[target];
			if (target_listener != NULL) {
				ASSERT(nlisteners < mm_core_getnum());
				listener->dispatch_targets[nlisteners++] = target_listener;
				dispatch->waiting_listeners[target] = NULL;
			}
		}

		// TODO: possibly wake any waiting listener if there are
		// pending changes.
		// if (!mm_event_batch_empty(&dispatch->pending_changes)) ...

		mm_regular_unlock(&dispatch->lock);

		for (unsigned int i = 0; i < nlisteners; i++) {
			struct mm_listener *target = listener->dispatch_targets[i];
			mm_listener_notify(target, dispatch);
		}

		// Attach each detached event sink for received events.
		for (unsigned int i = 0; i < listener->events.nevents; i++) {
			struct mm_event *event = &listener->events.events[i];
			struct mm_event_fd *ev_fd = event->ev_fd;
			if (ev_fd->target != core)
				continue;

			// For incomplete detach initiated by this thread
			// simply revert detach preparation.
			if (ev_fd->detach == core) {
				ev_fd->detach = MM_THREAD_NONE;
				continue;
			}

			// Wait for completion of detach initiated by another
			// thread.
			while (mm_memory_load(ev_fd->detach) != MM_THREAD_NONE)
				mm_spin_pause();
			mm_memory_fence();

			// Really attach at last.
			mm_event_dispatch(ev_fd, MM_EVENT_ATTACH);
		}

		// Finalize remaining detach events.
		mm_dispatch_detach(listener);

	} else {
		mm_regular_lock(&dispatch->lock);

		// Unregister as waiting listener.
		dispatch->waiting_listeners[core] = NULL;
		mm_dispatch_get_pending_events(dispatch, listener, core);

		mm_regular_unlock(&dispatch->lock);
	}

	LEAVE();
}
