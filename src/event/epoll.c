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
mm_event_epoll_add_event(struct mm_event_epoll *ev_ep,
			 struct mm_event *event,
			 struct mm_event_batch *events)
{
	struct mm_event_fd *ev_fd = event->ev_fd;

	int rc;
	struct epoll_event ee;
	ee.events = 0;
	ee.data.ptr = ev_fd;

	switch (event->event) {
	case MM_EVENT_REGISTER:
		if (ev_fd->input_handler)
			ee.events |= EPOLLIN | EPOLLET | EPOLLRDHUP;
		if (ev_fd->output_handler)
			ee.events |= EPOLLOUT | EPOLLET;

		rc = epoll_ctl(ev_ep->event_fd, EPOLL_CTL_ADD, ev_fd->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		mm_event_batch_add(events, MM_EVENT_REGISTER, ev_fd);
		break;

	case MM_EVENT_UNREGISTER:
		rc = epoll_ctl(ev_ep->event_fd, EPOLL_CTL_DEL, ev_fd->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		mm_event_batch_add(events, MM_EVENT_UNREGISTER, ev_fd);
		break;

	default:
		ABORT();
	}
}

static void
mm_event_epoll_get_events(struct mm_event_epoll *ev_ep, struct mm_event_batch *events)
{
	for (int i = 0; i < ev_ep->nevents; i++) {
		struct epoll_event *event = &ev_ep->events[i];
		struct mm_event_fd *ev_fd = event->data.ptr;

		if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
			mm_event_batch_add(events, MM_EVENT_INPUT_ERROR, ev_fd);
		else if ((event->events & EPOLLIN) != 0)
			mm_event_batch_add(events, MM_EVENT_INPUT, ev_fd);
		if ((event->events & EPOLLOUT) != 0)
			mm_event_batch_add(events, MM_EVENT_OUTPUT, ev_fd);
	}
}

static void
mm_event_epoll_poll(struct mm_event_epoll *ev_ep, mm_timeout_t timeout)
{
	ENTER();

	// Find the event wait timeout.
	timeout /= 1000;

	// Publish the log before a possible sleep.
	mm_log_relay();

	// Poll the system for events.
	int n = epoll_wait(ev_ep->event_fd,
			   ev_ep->events, MM_EVENT_EPOLL_NEVENTS,
			   timeout);

	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		ev_ep->nevents = 0;
	} else {
		ev_ep->nevents = n;
	}

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_epoll_prepare(struct mm_event_epoll *ev_ep)
{
	ENTER();

	// Open a epoll file descriptor.
	ev_ep->event_fd = epoll_create(511);
	if (ev_ep->event_fd < 0)
		mm_fatal(errno, "Failed to create epoll fd");

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_epoll_cleanup(struct mm_event_epoll *ev_ep)
{
	ENTER();

	// Close the epoll file descriptor.
	close(ev_ep->event_fd);

	LEAVE();
}

void __attribute__((nonnull(1, 2, 3)))
mm_event_epoll_listen(struct mm_event_epoll *ev_ep,
		      struct mm_event_batch *changes,
		      struct mm_event_batch *events,
		      mm_timeout_t timeout)
{
	ENTER();

	// Make event changes.
	for (unsigned int i = 0; i < changes->nevents; i++) {
		struct mm_event *event = &changes->events[i];
		mm_event_epoll_add_event(ev_ep, event, events);
	}

	// Poll for incoming events.
	mm_event_epoll_poll(ev_ep, timeout);

	// Store incoming events.
	mm_event_epoll_get_events(ev_ep, events);

	LEAVE();
}

#endif /* HAVE_SYS_EPOLL_H */
