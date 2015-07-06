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

	// Allocate listeners.
	dispatch->listeners = mm_common_calloc(nlisteners,
					       sizeof(struct mm_listener));
	for (mm_thread_t i = 0; i < nlisteners; i++)
		mm_listener_prepare(&dispatch->listeners[i], dispatch);
	dispatch->nlisteners = nlisteners;

	dispatch->polling_listener = MM_THREAD_NONE;
	mm_bitset_prepare(&dispatch->waiting_listeners,
			  &mm_common_space.xarena,
			  nlisteners);

	// Allocate pending event batches.
	dispatch->pending_events = mm_common_alloc(nlisteners * sizeof(struct mm_event_batch));
	for (mm_thread_t i = 0; i < nlisteners; i++)
		mm_event_batch_prepare(&dispatch->pending_events[i]);
	mm_event_batch_prepare(&dispatch->pending_changes);

	// Initialize system-specific resources.
	mm_event_backend_prepare(&dispatch->backend);

	// Register the self-pipe.
	mm_event_batch_add(&dispatch->pending_changes,
			   MM_EVENT_REGISTER,
			   &dispatch->backend.selfpipe.event_fd);
	mm_event_backend_listen(&dispatch->backend,
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

	// Release pending event batches.
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_event_batch_cleanup(&dispatch->pending_events[i]);
	mm_event_batch_cleanup(&dispatch->pending_changes);

	// Release listeners.
	mm_bitset_cleanup(&dispatch->waiting_listeners, &mm_common_space.xarena);
	for (mm_thread_t i = 0; i < dispatch->nlisteners; i++)
		mm_listener_cleanup(&dispatch->listeners[i]);
	mm_common_free(dispatch->listeners);

	// Release system-specific resources.
	mm_event_backend_cleanup(&dispatch->backend);

	LEAVE();
}

static void
mm_dispatch_check_events(struct mm_dispatch *dispatch,
			 struct mm_listener *listener,
			 mm_thread_t tid)
{
	// Prepare to detach finished event sinks.
	struct mm_event_batch *finish_events = &listener->finish;
	for (unsigned int i = 0; i < finish_events->nevents; i++) {
		struct mm_event *event = &finish_events->events[i];
		struct mm_event_fd *ev_fd = event->ev_fd;
		ev_fd->target = MM_THREAD_NONE;
		ev_fd->detach = tid;
	}

	struct mm_event_batch *pending_events = &dispatch->pending_events[tid];
	if (mm_event_batch_empty(pending_events))
		return;

	// Check if pending events affect any of the finished events.
	// Undo detach preparation in this case.
	for (unsigned int i = 0; i < pending_events->nevents; i++) {
		struct mm_event *event = &pending_events->events[i];
		struct mm_event_fd *ev_fd = event->ev_fd;
		if (ev_fd->detach != MM_THREAD_NONE) {
			ev_fd->detach = MM_THREAD_NONE;
			ev_fd->target = tid;
		}
	}

	// Grab pending incoming events.
	mm_event_batch_append(&listener->events, pending_events);
	mm_event_batch_clear(pending_events);
}

static void
mm_dispatch_get_pending_events(struct mm_dispatch *dispatch,
			       struct mm_listener *listener,
			       mm_thread_t tid)
{
	struct mm_event_batch *pending_events = &dispatch->pending_events[tid];
	if (mm_event_batch_empty(pending_events))
		return;

	// Grab pending incoming events.
	mm_event_batch_append(&listener->events, pending_events);
	mm_event_batch_clear(pending_events);
}

static void
mm_dispatch_detach_handle(struct mm_listener *listener)
{
	ENTER();

	struct mm_event_batch *finish_events = &listener->finish;
	for (unsigned int i = 0; i < finish_events->nevents; i++) {
		struct mm_event *event = &finish_events->events[i];
		struct mm_event_fd *ev_fd = event->ev_fd;
		if (ev_fd->detach != MM_THREAD_NONE) {
			ASSERT(ev_fd->target == MM_THREAD_NONE
			       || ev_fd->target == mm_thread_getnumber(mm_thread_self()));
			TRACE("detach from: %d", ev_fd->detach);
			mm_event_dispatch(ev_fd, MM_EVENT_DETACH);
			mm_memory_store_fence();
			mm_memory_store(ev_fd->detach, MM_THREAD_NONE);
		}
	}

	mm_event_batch_clear(finish_events);

	LEAVE();
}

static void __attribute__((nonnull(1, 2)))
mm_dispatch_checkin(struct mm_dispatch *dispatch,
		    struct mm_listener *listener,
		    mm_thread_t tid)
{
	ENTER();

	mm_regular_lock(&dispatch->lock);

	// The first arrived listener is elected to do event poll.
	if (dispatch->polling_listener == MM_THREAD_NONE) {
		// Register as a polling listener.
		dispatch->polling_listener = tid;

		// Seize all pending changes and make them private.
		mm_event_batch_append(&listener->changes, &dispatch->pending_changes);
		mm_event_batch_clear(&dispatch->pending_changes);

		mm_regular_unlock(&dispatch->lock);

		// Get pending incoming events and prepare detach events.
		mm_dispatch_check_events(dispatch, listener, tid);

	} else {
		// Register as a waiting listener.
		mm_bitset_set(&dispatch->waiting_listeners, tid);

		// Make private changes public adding them to pending changes.
		mm_thread_t notify_listener = MM_THREAD_NONE;
		if (mm_listener_has_changes(listener)) {
			mm_event_batch_append(&dispatch->pending_changes, &listener->changes);
			notify_listener = dispatch->polling_listener;
		}

		// Get pending incoming events and prepare detach events.
		mm_dispatch_check_events(dispatch, listener, tid);

		mm_regular_unlock(&dispatch->lock);

		// Finalize detach events.
		mm_dispatch_detach_handle(listener);

		// Wake up a listener that is possibly sleeping on a poll system call.
		if (notify_listener != MM_THREAD_NONE)
			mm_dispatch_notify(dispatch, notify_listener);
	}

	LEAVE();
}

static void __attribute__((nonnull(1, 2)))
mm_dispatch_checkout(struct mm_dispatch *dispatch,
		     struct mm_listener *listener,
		     mm_thread_t tid)
{
	ENTER();

	if (dispatch->polling_listener == tid) {
		unsigned int nlisteners = 0;

		mm_regular_lock(&dispatch->lock);

		// Unregister as polling listener.
		dispatch->polling_listener = MM_THREAD_NONE;

		// Dispatch received events.
		for (unsigned int i = 0; i < listener->events.nevents; i++) {
			struct mm_event *event = &listener->events.events[i];
			struct mm_event_fd *ev_fd = event->ev_fd;

			// Check to see if the event sink is attached to this
			// thread.
			mm_thread_t target = ev_fd->target;
			if (target == tid)
				continue;

			// Check to see if the event sink is detached. In this
			// case attach it to itself.
			if (target == MM_THREAD_NONE) {
				ev_fd->target = tid;
				continue;
			}

			// The event sink is attached to another thread.
			mm_event_batch_add(&dispatch->pending_events[target],
					   event->event, ev_fd);
			event->event = MM_EVENT_DISPATCH_STUB;

			if (mm_bitset_test(&dispatch->waiting_listeners, target)) {
				ASSERT(nlisteners < dispatch->nlisteners);
				listener->dispatch_targets[nlisteners++] = target;
				mm_bitset_clear(&dispatch->waiting_listeners, target);
			}
		}

		// TODO: possibly wake any waiting listener if there are
		// pending changes.
		// if (!mm_event_batch_empty(&dispatch->pending_changes)) ...

		mm_regular_unlock(&dispatch->lock);

		for (unsigned int i = 0; i < nlisteners; i++) {
			mm_thread_t target = listener->dispatch_targets[i];
			mm_dispatch_notify(dispatch, target);
		}

		// Attach each detached event sink for received events.
		for (unsigned int i = 0; i < listener->events.nevents; i++) {
			struct mm_event *event = &listener->events.events[i];
			struct mm_event_fd *ev_fd = event->ev_fd;
			if (ev_fd->target != tid)
				continue;

			// For incomplete detach initiated by this thread
			// simply revert detach preparation.
			if (ev_fd->detach == tid) {
				ev_fd->detach = MM_THREAD_NONE;
				continue;
			}

			// Wait for completion of detach initiated by another
			// thread.
			while (mm_memory_load(ev_fd->detach) != MM_THREAD_NONE)
				mm_spin_pause();
			mm_memory_fence();

			// Really attach at last.
			TRACE("attach to: %d", ev_fd->target);
			mm_event_dispatch(ev_fd, MM_EVENT_ATTACH);
		}

		// Finalize remaining detach events.
		mm_dispatch_detach_handle(listener);

	} else {
		mm_regular_lock(&dispatch->lock);

		// Unregister as waiting listener.
		mm_bitset_clear(&dispatch->waiting_listeners, tid);
		mm_dispatch_get_pending_events(dispatch, listener, tid);

		mm_regular_unlock(&dispatch->lock);
	}

	// Handle received events.
	for (unsigned int i = 0; i < listener->events.nevents; i++) {
		struct mm_event *event = &listener->events.events[i];
		mm_event_dispatch(event->ev_fd, event->event);
	}

	// Forget just handled events.
	mm_event_batch_clear(&listener->changes);
	mm_event_batch_clear(&listener->events);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_dispatch_listen(struct mm_dispatch *dispatch, mm_thread_t tid,
		   mm_timeout_t timeout)
{
	ENTER();
	ASSERT(tid < dispatch->nlisteners);
	struct mm_listener *listener = &dispatch->listeners[tid];

	// Register the listener for event dispatch.
	mm_dispatch_checkin(dispatch, listener, tid);

	// Check to see if the listener has been elected to do event poll.
	bool polling = (tid == dispatch->polling_listener);

	// Wait for events.
	if (polling)
		mm_listener_listen(listener, &dispatch->backend, timeout);
	else
		mm_listener_listen(listener, NULL, timeout);

	// Unregister the listener from event dispatch.
	mm_dispatch_checkout(dispatch, listener, tid);

	LEAVE();
}
