/*
 * base/event/epoll.c - MainMemory epoll support.
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

#include "base/event/epoll.h"

#if HAVE_SYS_EPOLL_H

#include "base/event/dispatch.h"
#include "base/event/nonblock.h"
#include "base/event/listener.h"

#define MM_EVENT_EPOLL_NOTIFY_SINK ((struct mm_event_fd *) -1)

/**********************************************************************
 * Helper routines for handling incoming events.
 **********************************************************************/

static void
mm_event_epoll_handle(struct mm_event_epoll *backend, struct mm_event_listener *listener, int nevents)
{
	mm_event_listener_handle_start(listener, nevents);

	for (int i = 0; i < nevents; i++) {
		struct epoll_event *event = &listener->backend.events[i];
		struct mm_event_fd *sink = event->data.ptr;

		if ((event->events & EPOLLIN) != 0) {
			if (sink == MM_EVENT_EPOLL_NOTIFY_SINK) {
				backend->notified = true;
				listener->notifications++;
			} else {
				mm_event_listener_input(listener, sink);
			}
		}

		if ((event->events & EPOLLOUT) != 0) {
			mm_event_listener_output(listener, sink);
		}

		if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
			bool input = sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER);
			bool output = sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER);
			if (input)
				mm_event_listener_input_error(listener, sink);
			if (output && (event->events & (EPOLLERR | EPOLLHUP)) != 0)
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

	// Notification is disabled by default.
	backend->notify_fd = -1;
	backend->notified = false;

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_cleanup(struct mm_event_epoll *backend)
{
	ENTER();

	// Close the eventfd file descriptor.
	if (backend->notify_fd >= 0)
		mm_close(backend->notify_fd);

	// Close the epoll file descriptor.
	mm_close(backend->event_fd);

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_local_prepare(struct mm_event_epoll_local *local UNUSED)
{
	ENTER();

#if ENABLE_EVENT_STATS
	for (size_t i = 0; i <= MM_EVENT_EPOLL_NEVENTS; i++)
		local->nevents_stats[i] = 0;
#endif

	LEAVE();
}

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_epoll_poll(struct mm_event_epoll *backend, struct mm_event_epoll_local *local, mm_timeout_t timeout)
{
	ENTER();

	struct mm_event_listener *const listener = containerof(local, struct mm_event_listener, backend);

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
	int n = mm_epoll_wait(backend->event_fd, local->events, MM_EVENT_EPOLL_NEVENTS, timeout);
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
		mm_event_epoll_handle(backend, listener, n);
	}

#if ENABLE_EVENT_STATS
	local->nevents_stats[n]++;
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_enable_notify(struct mm_event_epoll *backend)
{
	ENTER();

	// Create a file descriptor for notifications.
	int fd = mm_eventfd(0, 0);
	if (fd < 0)
		mm_fatal(errno, "eventfd");
	backend->notify_fd = fd;

	// Set it up for non-blocking I/O.
	mm_set_nonblocking(fd);

	// Register the event sink.
	struct epoll_event ee;
	ee.events = EPOLLIN | EPOLLET;
	ee.data.ptr = MM_EVENT_EPOLL_NOTIFY_SINK;
	int er = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, fd, &ee);
	if (unlikely(er < 0))
		mm_fatal(errno, "epoll_ctl");

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_notify(struct mm_event_epoll *backend)
{
	ENTER();

	uint64_t value = 1;
	int n = mm_write(backend->notify_fd, &value, sizeof value);
	if (unlikely(n != sizeof value))
		mm_fatal(errno, "eventfd write");

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_notify_clean(struct mm_event_epoll *backend)
{
	ENTER();

	if (backend->notified) {
		backend->notified = false;

		uint64_t value;
		int n = mm_read(backend->notify_fd, &value, sizeof value);
		if (n != sizeof value)
			mm_warning(errno, "eventfd read");
	}

	LEAVE();
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_epoll_unregister_fd(struct mm_event_epoll *backend, struct mm_event_epoll_local *local, struct mm_event_fd *sink)
{
	ENTER();

	struct mm_event_listener *listener = containerof(local, struct mm_event_listener, backend);
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

#endif /* HAVE_SYS_EPOLL_H */
