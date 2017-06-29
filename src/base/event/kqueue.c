/*
 * base/event/kqueue.c - MainMemory kqueue support.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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
#include "base/fiber/strand.h"

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
mm_event_kqueue_adjust(struct mm_event_listener *listener, int nevents)
{
	if (!mm_event_listener_adjust_start(listener, nevents))
		return;

	for (int i = 0; i < nevents; i++) {
		struct kevent *event = &listener->storage.revents[i];
		if (event->filter == EVFILT_READ || event->filter == EVFILT_WRITE) {
			struct mm_event_fd *sink = event->udata;
			if (!mm_event_listener_adjust(listener, sink))
				return;
		}
	}
}

static void
mm_event_kqueue_handle(struct mm_event_listener *listener, int nevents)
{
	for (int i = 0; i < nevents; i++) {
		struct kevent *event = &listener->storage.revents[i];

		if (event->filter == EVFILT_READ) {
			DEBUG("read event: fd %d", (int) event->ident);

			struct mm_event_fd *sink = event->udata;
			if (unlikely((event->flags & EV_ERROR) != 0))
				mm_warning(event->data, "kevent change failed");
			else if ((event->flags & EV_EOF) != 0)
				mm_event_listener_input_error(listener, sink);
			else
				mm_event_listener_input(listener, sink);

		} else if (event->filter == EVFILT_WRITE) {
			DEBUG("write event: fd %d", (int) event->ident);

			struct mm_event_fd *sink = event->udata;
			if (unlikely((event->flags & EV_ERROR) != 0))
				mm_warning(event->data, "kevent change failed");
			else if ((event->flags & EV_EOF) != 0)
				mm_event_listener_output_error(listener, sink);
			else
				mm_event_listener_output(listener, sink);

		} else if (event->filter == EVFILT_USER) {
			ASSERT(event->ident == MM_EVENT_KQUEUE_NOTIFY_ID);
		}
	}
}

static void
mm_event_kqueue_finish_changes(struct mm_event_listener *listener)
{
	listener->storage.nevents = 0;

	// Reset change flags.
	const uint32_t nchanges = listener->storage.nchanges;
	if (nchanges) {
		listener->storage.nchanges = 0;
		for (uint32_t i = 0; i < nchanges; i++) {
			listener->storage.changes[i]->status = MM_EVENT_ENABLED;
		}
	}

	// Handle unregistered sinks.
	const uint32_t nunregister = listener->storage.nunregister;
	if (nunregister) {
		listener->storage.nunregister = 0;
		for (uint32_t i = 0; i < nunregister; i++) {
			mm_event_listener_unregister(listener, listener->storage.unregister[i]);
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

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_cleanup(struct mm_event_kqueue *backend)
{
	ENTER();

	// Close the kqueue file descriptor.
	mm_close(backend->event_fd);

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_storage_prepare(struct mm_event_kqueue_storage *storage)
{
	ENTER();

	storage->nevents = 0;
	storage->nchanges = 0;
	storage->nunregister = 0;

#if ENABLE_EVENT_STATS
	for (size_t i = 0; i <= MM_EVENT_KQUEUE_NEVENTS; i++)
		storage->nevents_stats[i] = 0;
#endif

	LEAVE();
}

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_kqueue_poll(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage,
		     mm_timeout_t timeout)
{
	ENTER();

	struct mm_event_listener *listener = containerof(storage, struct mm_event_listener, storage);

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
	int n = mm_kevent(backend->event_fd, storage->events, storage->nevents,
			  storage->revents, MM_EVENT_KQUEUE_NEVENTS, &ts);
	DEBUG("kevent changed: %d, received: %d", storage->nevents, n);
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
		mm_event_kqueue_adjust(listener, n);
		mm_event_listener_handle_start(listener, n);
		mm_event_kqueue_handle(listener, n);
		mm_event_listener_handle_finish(listener);
	} else if (mm_memory_load(listener->dispatch->sink_queue_num) != 0) {
		mm_event_listener_handle_queued(listener);
	}

#if ENABLE_EVENT_STATS
	storage->nevents_stats[n]++;
#endif

	LEAVE();
}

#if MM_EVENT_NATIVE_NOTIFY

bool NONNULL(1)
mm_event_kqueue_enable_notify(struct mm_event_kqueue *backend)
{
	ENTER();
	bool rc = true;

	static const struct kevent event = {
		.filter = EVFILT_USER,
		.ident = MM_EVENT_KQUEUE_NOTIFY_ID,
		.flags = EV_ADD | EV_CLEAR,
	};

	int n = mm_kevent(backend->event_fd, &event, 1, NULL, 0, NULL);
	DEBUG("kevent notify: %d", n);
	if (unlikely(n < 0)) {
		mm_warning(errno, "kevent");
		rc = false;
	}

	LEAVE();
	return rc;
}

void NONNULL(1)
mm_event_kqueue_notify(struct mm_event_kqueue *backend)
{
	ENTER();

	static const struct kevent event = {
		.filter = EVFILT_USER,
		.ident = MM_EVENT_KQUEUE_NOTIFY_ID,
		.fflags = NOTE_TRIGGER,
	};

	int n = mm_kevent(backend->event_fd, &event, 1, NULL, 0, NULL);
	DEBUG("kevent notify: %d", n);
	if (unlikely(n < 0))
		mm_error(errno, "kevent");

	LEAVE();
}

#endif /* MM_EVENT_NATIVE_NOTIFY */

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_kqueue_flush(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage)
{
	ENTER();

	struct mm_event_listener *listener = containerof(storage, struct mm_event_listener, storage);
	struct mm_event_dispatch *dispatch = containerof(backend, struct mm_event_dispatch, backend);

	// Enter the state that forbids fiber yield to avoid possible
	// problems with re-entering from another fiber.
	// TODO: don't call mm_strand_selfptr(), get it through the listener
	struct mm_strand *strand = mm_strand_selfptr();
	mm_strand_state_t strand_state = strand->state;
	strand->state = MM_STRAND_CSWITCH;

	// If any event sinks are to be unregistered then start a reclamation epoch.
	if (storage->nunregister != 0)
		mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	// Submit change events.
	int n = mm_kevent(backend->event_fd, storage->events, storage->nevents, NULL, 0, NULL);
	DEBUG("kevent changed: %d, received: %d", storage->nevents, n);
	if (unlikely(n < 0))
		mm_error(errno, "kevent");

	// Finish pending changes.
	mm_event_kqueue_finish_changes(listener);

	// If a reclamation epoch is active then attempt to advance it and possibly finish.
	if (mm_event_epoch_active(&listener->epoch))
		mm_event_epoch_advance(&listener->epoch, &dispatch->global_epoch);

	// Restore normal running state.
	strand->state = strand_state;

	LEAVE();
}

#endif /* HAVE_SYS_EVENT_H */
