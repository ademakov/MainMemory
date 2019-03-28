/*
 * base/event/epoll.c - MainMemory epoll support.
 *
 * Copyright (C) 2012-2018  Aleksey Demakov
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

#include "base/event/epoll.h"

#if HAVE_SYS_EPOLL_H

#include "base/event/dispatch.h"
#include "base/event/nonblock.h"
#include "base/event/listener.h"

#if HAVE_SYS_EVENTFD_H
# include <sys/eventfd.h>
#endif

/**********************************************************************
 * Helper routines for handling incoming events.
 **********************************************************************/

static void
mm_event_epoll_handle(struct mm_event_listener *listener, int nevents)
{
	mm_event_listener_handle_start(listener, nevents);

	for (int i = 0; i < nevents; i++, mm_event_listener_handle_next(listener)) {
		struct epoll_event *event = &listener->storage.events[i];
		struct mm_event_fd *sink = event->data.ptr;

		if ((event->events & EPOLLIN) != 0)
			mm_event_listener_input(listener, sink);

		if ((event->events & EPOLLOUT) != 0)
			mm_event_listener_output(listener, sink);

		if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
			bool enable_input = sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_ONESHOT_INPUT);
			bool enable_output = sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_ONESHOT_OUTPUT);
			if (enable_input)
				mm_event_listener_input_error(listener, sink);
			if (enable_output && (event->events & (EPOLLERR | EPOLLHUP)) != 0)
				mm_event_listener_output_error(listener, sink);
		}
	}

	mm_event_listener_handle_finish(listener);
}

/**********************************************************************
 * Event backend initialization and cleanup.
 **********************************************************************/

void NONNULL(1)
mm_event_epoll_prepare(struct mm_event_epoll *backend)
{
	ENTER();

	// Open a epoll file descriptor.
	backend->event_fd = epoll_create(511);
	if (backend->event_fd < 0)
		mm_fatal(errno, "failed to create epoll fd");

	// Mark the evenefd file descriptor as unused.
	backend->notify_fd.fd = -1;

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_cleanup(struct mm_event_epoll *backend)
{
	ENTER();

	if (backend->notify_fd.fd >= 0)
		mm_close(backend->notify_fd.fd);

	// Close the epoll file descriptor.
	mm_close(backend->event_fd);

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_storage_prepare(struct mm_event_epoll_storage *storage UNUSED)
{
	ENTER();

#if ENABLE_EVENT_STATS
	for (size_t i = 0; i <= MM_EVENT_EPOLL_NEVENTS; i++)
		storage->nevents_stats[i] = 0;
#endif

	LEAVE();
}

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_epoll_poll(struct mm_event_epoll *backend, struct mm_event_epoll_storage *storage,
		    mm_timeout_t timeout)
{
	ENTER();

	struct mm_event_listener *listener = containerof(storage, struct mm_event_listener, storage);

	if (timeout) {
		// Announce that the thread is about to sleep.
		mm_stamp_t stamp = mm_event_listener_polling(listener);
		if (!mm_event_listener_restful(listener, stamp)) {
			timeout = 0;
		} else {
			// Calculate the event wait timeout.
			timeout /= 1000;
		}
	}

	// Poll the system for events.
	int n = mm_epoll_wait(backend->event_fd, storage->events, MM_EVENT_EPOLL_NEVENTS, timeout);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		n = 0;
	}

	// Announce the start of another working cycle.
	mm_event_listener_running(listener);

	// Handle incoming events.
	if (n != 0) {
		mm_event_epoll_handle(listener, n);
	}

#if ENABLE_EVENT_STATS
	storage->nevents_stats[n]++;
#endif

	LEAVE();
}

#if MM_EVENT_NATIVE_NOTIFY

bool NONNULL(1)
mm_event_epoll_enable_notify(struct mm_event_epoll *backend)
{
	ENTER();
	bool rc = true;

	// Create a file descriptor for notifications.
	int fd = mm_eventfd(0, 0);
	if (fd < 0) {
		mm_warning(errno, "eventfd");
		rc = false;
		goto leave;
	}

	// Set it up for non-blocking I/O.
	mm_set_nonblocking(fd);

	// Initialize the corresponding event sink.
	mm_event_prepare_fd(&backend->notify_fd, fd, mm_event_instant_io(),
			    MM_EVENT_IGNORED, MM_EVENT_IGNORED, MM_EVENT_REGULAR_INPUT | MM_EVENT_NOTIFY_FD);

	// Register the event sink.
	struct epoll_event ee;
	ee.events = EPOLLIN | EPOLLET;
	ee.data.ptr = &backend->notify_fd;
	int er = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, backend->notify_fd.fd, &ee);
	if (unlikely(er < 0))
		mm_fatal(errno, "epoll_ctl");

leave:
	LEAVE();
	return rc;
}

void NONNULL(1)
mm_event_epoll_notify(struct mm_event_epoll *backend)
{
	ENTER();

	uint64_t value = 1;
	int n = mm_write(backend->notify_fd.fd, &value, sizeof value);
	if (unlikely(n != sizeof value))
		mm_fatal(errno, "eventfd write");

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_notify_clean(struct mm_event_epoll *backend)
{
	ENTER();

	if ((backend->notify_fd.flags & MM_EVENT_INPUT_READY) != 0) {
		backend->notify_fd.flags &= ~MM_EVENT_INPUT_READY;

		uint64_t value;
		int n = mm_read(backend->notify_fd.fd, &value, sizeof value);
		if (n != sizeof value)
			mm_warning(errno, "eventfd read");
	}

	LEAVE();
}

#endif /* MM_EVENT_NATIVE_NOTIFY */

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_epoll_unregister_fd(struct mm_event_epoll *backend, struct mm_event_epoll_storage *storage,
			     struct mm_event_fd *sink)
{
	ENTER();

	struct mm_event_listener *listener = containerof(storage, struct mm_event_listener, storage);
	struct mm_event_dispatch *dispatch = containerof(backend, struct mm_event_dispatch, backend);

	// Start a reclamation epoch.
	mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	// Delete the file descriptor from epoll.
	if ((sink->flags
	     & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER
		| MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER)) != 0) {
		struct epoll_event ee;
		int rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_DEL, sink->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
	}

	// Finish unregister call sequence.
	mm_event_listener_unregister(listener, sink);

	// Attempt to advance the reclamation epoch and possibly finish it.
	mm_event_epoch_advance(&listener->epoch, &dispatch->global_epoch);

	LEAVE();
}

/**********************************************************************
 * Interface for handling events delivered to the target thread.
 **********************************************************************/

void NONNULL(1)
mm_event_epoll_reset_input(struct mm_event_fd *sink)
{
	ENTER();

	struct mm_event_listener *listener = sink->listener;
	struct mm_event_dispatch *dispatch = listener->dispatch;
	struct mm_event_epoll *backend = &dispatch->backend.backend;

	mm_event_epoll_disable_input(backend, sink);

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_reset_output(struct mm_event_fd *sink)
{
	ENTER();

	struct mm_event_listener *listener = sink->listener;
	struct mm_event_dispatch *dispatch = listener->dispatch;
	struct mm_event_epoll *backend = &dispatch->backend.backend;

	mm_event_epoll_disable_output(backend, sink);

	LEAVE();
}

#endif /* HAVE_SYS_EPOLL_H */
