/*
 * base/event/kqueue.c - MainMemory kqueue support.
 *
 * Copyright (C) 2012-2020  Aleksey Demakov
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

#include "base/event/kqueue.h"

#if HAVE_SYS_EVENT_H

#include "base/report.h"
#include "base/stdcall.h"
#include "base/event/dispatch.h"
#include "base/event/listener.h"

#include <time.h>

#define MM_EVENT_KQUEUE_NOTIFY_ID	123

/**********************************************************************
 * Wrappers for kqueue system calls.
 **********************************************************************/

#if ENABLE_INLINE_SYSCALLS

static inline int
mm_kqueue(void)
{
	return mm_syscall_0(MM_SYSCALL_N(SYS_kqueue));
}

static inline int
mm_kevent(int kq, const struct kevent *changes, int nchanges,
	  struct kevent *events, int nevents, const struct timespec *ts)
{
	return mm_syscall_6(MM_SYSCALL_N(SYS_kevent),
			    kq, (uintptr_t) changes, nchanges,
			    (uintptr_t) events, nevents, (uintptr_t) ts);
}

#else

# define mm_kqueue	kqueue
# define mm_kevent	kevent

#endif

/**********************************************************************
 * Helper routines for handling incoming events.
 **********************************************************************/

static void
mm_event_kqueue_handle(struct mm_event_kqueue *backend, struct mm_event_listener *listener, int nevents)
{
	mm_event_listener_handle_start(listener, nevents);

	for (int i = 0; i < nevents; i++) {
		struct kevent *event = &listener->backend.revents[i];

		if (event->filter == EVFILT_READ) {
			DEBUG("read event: fd %d", (int) event->ident);
			if (unlikely((event->flags & EV_ERROR) != 0)) {
				mm_warning(event->data, "kevent change failed");
				continue;
			}
			if (event->ident == (uintptr_t) backend->selfpipe.read_fd) {
				mm_selfpipe_set_notified(&backend->selfpipe);
				listener->notifications++;
				continue;
			}

			struct mm_event_fd *sink = event->udata;
			if ((event->flags & EV_EOF) != 0)
				mm_event_listener_input_error(listener, sink);
			else
				mm_event_listener_input(listener, sink);

		} else if (event->filter == EVFILT_WRITE) {
			DEBUG("write event: fd %d", (int) event->ident);
			if (unlikely((event->flags & EV_ERROR) != 0)) {
				mm_warning(event->data, "kevent change failed");
				continue;
			}

			struct mm_event_fd *sink = event->udata;
			if ((event->flags & EV_EOF) != 0)
				mm_event_listener_output_error(listener, sink);
			else
				mm_event_listener_output(listener, sink);

		} else if (event->filter == EVFILT_USER) {
			ASSERT(event->ident == MM_EVENT_KQUEUE_NOTIFY_ID);
		}
	}

	mm_event_listener_handle_finish(listener);
}

static void
mm_event_kqueue_finish_changes(struct mm_event_listener *listener)
{
	listener->backend.nevents = 0;

	// Reset change flags.
	const uint32_t nchanges = listener->backend.nchanges;
	if (nchanges) {
		listener->backend.nchanges = 0;
		for (uint32_t i = 0; i < nchanges; i++) {
			listener->backend.changes[i]->flags |= ~MM_EVENT_CHANGE;
		}
	}

	// Handle unregistered sinks.
	const uint32_t nunregister = listener->backend.nunregister;
	if (nunregister) {
		listener->backend.nunregister = 0;
		for (uint32_t i = 0; i < nunregister; i++) {
			mm_event_listener_unregister(listener, listener->backend.unregister[i]);
		}
	}
}

/**********************************************************************
 * Event backend initialization and cleanup.
 **********************************************************************/

void NONNULL(1)
mm_event_kqueue_prepare(struct mm_event_kqueue *backend)
{
	ENTER();

	// Open a kqueue file descriptor.
	backend->event_fd = mm_kqueue();
	if (backend->event_fd == -1)
		mm_fatal(errno, "Failed to create kqueue");

	// Notification is disabled by default.
	backend->notify_enabled = false;
	backend->selfpipe.read_fd = -1;
	backend->selfpipe.write_fd = -1;

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_cleanup(struct mm_event_kqueue *backend)
{
	ENTER();

	// Close the event self-pipe if needed.
#if MM_EVENT_NATIVE_NOTIFY
	if (backend->native_notify) {
		// do nothing
	} else if (backend->notify_enabled) {
		mm_selfpipe_cleanup(&backend->selfpipe);
	}
#else
	if (backend->notify_enabled) {
		mm_selfpipe_cleanup(&backend->selfpipe);
	}
#endif

	// Close the kqueue file descriptor.
	mm_close(backend->event_fd);

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_local_prepare(struct mm_event_kqueue_local *local)
{
	ENTER();

	local->nevents = 0;
	local->nchanges = 0;
	local->nunregister = 0;

#if ENABLE_EVENT_STATS
	for (size_t i = 0; i <= MM_EVENT_KQUEUE_NEVENTS; i++)
		local->nevents_stats[i] = 0;
#endif

	LEAVE();
}

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_kqueue_poll(struct mm_event_kqueue *backend, struct mm_event_kqueue_local *local, mm_timeout_t timeout)
{
	ENTER();

	struct mm_event_listener *listener = containerof(local, struct mm_event_listener, backend);

	// Announce that the thread is about to sleep.
	if (timeout) {
		mm_stamp_t stamp = mm_event_listener_polling(listener);
		if (!mm_event_listener_restful(listener, stamp))
			timeout = 0;
	}

	// Calculate the event wait timeout.
	struct timespec ts;
	if (timeout) {
		ts.tv_sec = timeout / 1000000;
		ts.tv_nsec = (timeout % 1000000) * 1000;
	} else {
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
	}

	// Poll the system for events.
	int n = mm_kevent(backend->event_fd, local->events, local->nevents,
			  local->revents, MM_EVENT_KQUEUE_NEVENTS, &ts);
	DEBUG("kevent changed: %d, received: %d", local->nevents, n);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "kevent");
		else
			mm_error(errno, "kevent");
		n = 0;
	}

	// Announce the start of another working cycle.
	mm_event_listener_running(listener);

	// Finish pending changes to let event handlers make new changes
	// without problems.
	mm_event_kqueue_finish_changes(listener);

	// Handle incoming events.
	if (n != 0) {
		mm_event_kqueue_handle(backend, listener, n);
	}

#if ENABLE_EVENT_STATS
	local->nevents_stats[n]++;
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_enable_notify(struct mm_event_kqueue *backend)
{
	ENTER();

#if MM_EVENT_NATIVE_NOTIFY

	bool native_notify = true;

	static const struct kevent event = {
		.filter = EVFILT_USER,
		.ident = MM_EVENT_KQUEUE_NOTIFY_ID,
		.flags = EV_ADD | EV_CLEAR,
	};

	int n = mm_kevent(backend->event_fd, &event, 1, NULL, 0, NULL);
	if (unlikely(n < 0)) {
		mm_warning(errno, "kevent");
		native_notify = false;
	}

	backend->native_notify = native_notify;

#else /* MM_EVENT_NATIVE_NOTIFY */

	bool native_notify = false;

#endif /* MM_EVENT_NATIVE_NOTIFY */

	if (!native_notify) {
		// Open the event self-pipe.
		mm_selfpipe_prepare(&backend->selfpipe);

		// Register it with kqueue.
		struct kevent event;
		EV_SET(&event, backend->selfpipe.read_fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
		int n = mm_kevent(backend->event_fd, &event, 1, NULL, 0, NULL);
		if (unlikely(n < 0)) {
			mm_fatal(errno, "kevent");
			native_notify = false;
		}
	}

	backend->notify_enabled = true;

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_notify(struct mm_event_kqueue *backend)
{
	ENTER();
	ASSERT(backend->notify_enabled);

#if MM_EVENT_NATIVE_NOTIFY

	if (backend->native_notify) {
		static const struct kevent event = {
				.filter = EVFILT_USER,
				.ident = MM_EVENT_KQUEUE_NOTIFY_ID,
				.fflags = NOTE_TRIGGER,
		};

		int n = mm_kevent(backend->event_fd, &event, 1, NULL, 0, NULL);
		DEBUG("kevent notify: %d", n);
		if (unlikely(n < 0))
			mm_error(errno, "kevent");
	} else {
		mm_selfpipe_notify(&backend->selfpipe);
	}

#else /* MM_EVENT_NATIVE_NOTIFY */

	mm_selfpipe_notify(&backend->selfpipe);

#endif /* MM_EVENT_NATIVE_NOTIFY */

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_notify_clean(struct mm_event_kqueue *backend)
{
	ENTER();
	ASSERT(backend->notify_enabled);

#if MM_EVENT_NATIVE_NOTIFY

	if (backend->native_notify) {
		// do nothing
	} else if (mm_selfpipe_is_notified(&backend->selfpipe)) {
		mm_selfpipe_absorb(&backend->selfpipe);
	}

#else /* MM_EVENT_NATIVE_NOTIFY */

	if (mm_selfpipe_is_notified(&backend->selfpipe)) {
		mm_selfpipe_absorb(&backend->selfpipe);
	}

#endif /* MM_EVENT_NATIVE_NOTIFY */

	LEAVE();
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_kqueue_flush(struct mm_event_kqueue *backend, struct mm_event_kqueue_local *local)
{
	ENTER();

	struct mm_event_listener *listener = containerof(local, struct mm_event_listener, backend);
	struct mm_event_dispatch *dispatch = containerof(backend, struct mm_event_dispatch, backend);

	// Enter the state that forbids fiber yield to avoid possible
	// problems with re-entering from another fiber.
	struct mm_context *const context = listener->context;
	context->status = MM_CONTEXT_PENDING;

	// If any event sinks are to be unregistered then start a reclamation epoch.
	if (local->nunregister != 0)
		mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	// Submit change events.
	int n = mm_kevent(backend->event_fd, local->events, local->nevents, NULL, 0, NULL);
	DEBUG("kevent changed: %d, received: %d", local->nevents, n);
	if (unlikely(n < 0))
		mm_error(errno, "kevent");

	// Finish pending changes.
	mm_event_kqueue_finish_changes(listener);

	// If a reclamation epoch is active then attempt to advance it and possibly finish.
	if (mm_event_epoch_active(&listener->epoch))
		mm_event_epoch_advance(&listener->epoch, &dispatch->global_epoch);

	// Restore normal running state.
	context->status = MM_CONTEXT_RUNNING;

	LEAVE();
}

#endif /* HAVE_SYS_EVENT_H */
