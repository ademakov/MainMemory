/*
 * event/epoll.c - MainMemory epoll support.
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

#include "event/epoll.h"
#include "event/batch.h"
#include "event/event.h"

#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/trace.h"

#include <unistd.h>

#if HAVE_SYS_EPOLL_H

static void
mm_event_epoll_add_event(struct mm_event_epoll *event_backend,
			 struct mm_event *change_event,
			 struct mm_event_batch *return_events)
{
	struct mm_event_fd *ev_fd = change_event->ev_fd;

	int rc;
	struct epoll_event ee;
	ee.events = 0;
	ee.data.ptr = ev_fd;

	switch (change_event->event) {
	case MM_EVENT_REGISTER:
		if (ev_fd->input_handler)
			ee.events |= EPOLLIN | EPOLLET | EPOLLRDHUP;
		if (ev_fd->output_handler)
			ee.events |= EPOLLOUT | EPOLLET;

		rc = epoll_ctl(event_backend->event_fd, EPOLL_CTL_ADD, ev_fd->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		mm_event_batch_add(return_events, MM_EVENT_REGISTER, ev_fd);
		break;

	case MM_EVENT_UNREGISTER:
		rc = epoll_ctl(event_backend->event_fd, EPOLL_CTL_DEL, ev_fd->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		mm_event_batch_add(return_events, MM_EVENT_UNREGISTER, ev_fd);
		break;

	default:
		ABORT();
	}
}

static void
mm_event_epoll_get_events(struct mm_event_batch *return_events,
			  struct mm_event_epoll *event_backend)
{
	for (int i = 0; i < event_backend->nevents; i++) {
		struct epoll_event *event = &event_backend->events[i];
		struct mm_event_fd *ev_fd = event->data.ptr;

		if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
			mm_event_batch_add(return_events, MM_EVENT_INPUT_ERROR, ev_fd);
		else if ((event->events & EPOLLIN) != 0)
			mm_event_batch_add(return_events, MM_EVENT_INPUT, ev_fd);
		if ((event->events & EPOLLOUT) != 0)
			mm_event_batch_add(return_events, MM_EVENT_OUTPUT, ev_fd);
	}
}

static void
mm_event_epoll_poll(struct mm_event_epoll *event_backend, mm_timeout_t timeout)
{
	ENTER();

	// Find the event wait timeout.
	timeout /= 1000;

	// Publish the log before a possible sleep.
	mm_log_relay();

	// Poll the system for events.
	int n = epoll_wait(event_backend->event_fd,
			   event_backend->events, MM_EVENT_EPOLL_NEVENTS,
			   timeout);

	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		event_backend->nevents = 0;
	} else {
		event_backend->nevents = n;
	}

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_epoll_prepare(struct mm_event_epoll *event_backend)
{
	ENTER();

	// Open a epoll file descriptor.
	event_backend->event_fd = epoll_create(511);
	if (event_backend->event_fd < 0)
		mm_fatal(errno, "Failed to create epoll fd");

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_epoll_cleanup(struct mm_event_epoll *event_backend)
{
	ENTER();

	// Close the epoll file descriptor.
	close(event_backend->event_fd);

	LEAVE();
}

void __attribute__((nonnull(1, 2, 3)))
mm_event_epoll_listen(struct mm_event_epoll *event_backend,
		      struct mm_event_batch *change_events,
		      struct mm_event_batch *return_events,
		      mm_timeout_t timeout)
{
	ENTER();

	// Make event changes.
	for (unsigned int i = 0; i < change_events->nevents; i++) {
		struct mm_event *change_event = &change_events->events[i];
		mm_event_epoll_add_event(event_backend, change_event, return_events);
	}

	// Poll for incoming events.
	mm_event_epoll_poll(event_backend, timeout);

	// Store incoming events.
	mm_event_epoll_get_events(return_events, event_backend);

	LEAVE();
}

#endif /* HAVE_SYS_EPOLL_H */
