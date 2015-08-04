/*
 * base/event/epoll.c - MainMemory epoll support.
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

#include "base/event/epoll.h"

#if HAVE_SYS_EPOLL_H

#include "base/stdcall.h"
#include "base/event/batch.h"
#include "base/event/event.h"
#include "base/event/receiver.h"
#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/trace.h"

#if ENABLE_INLINE_SYSCALLS
static inline int
mm_epoll_create(int n)
{
	return mm_syscall_1(SYS_epoll_create, n);
}

static inline int
mm_epoll_ctl(int ep, int op, int fd, struct epoll_event *event)
{
	return mm_syscall_4(SYS_epoll_ctl, ep, op, fd, (uintptr_t) event);
}

static inline int
mm_epoll_wait(int ep, struct epoll_event *events, int nevents, int timeout)
{
	return mm_syscall_4(SYS_epoll_wait, ep,
			    (uintptr_t) events, nevents,
			    timeout);
}

#else

# define mm_epoll_create	epoll_create
# define mm_epoll_ctl		epoll_ctl
# define mm_epoll_wait		epoll_wait

#endif

static void
mm_event_epoll_add_event(struct mm_event_epoll *event_backend,
			 struct mm_event *change_event,
			 struct mm_event_receiver *return_events)
{
	struct mm_event_fd *ev_fd = change_event->ev_fd;

	int rc;
	struct epoll_event ee;
	ee.events = 0;
	ee.data.ptr = ev_fd;

	switch (change_event->event) {
	case MM_EVENT_REGISTER:
		if (ev_fd->regular_input)
			ee.events |= EPOLLIN | EPOLLET | EPOLLRDHUP;
		if (ev_fd->regular_output)
			ee.events |= EPOLLOUT | EPOLLET;

		rc = mm_epoll_ctl(event_backend->event_fd, EPOLL_CTL_ADD,
				  ev_fd->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
		break;

	case MM_EVENT_UNREGISTER:
		rc = mm_epoll_ctl(event_backend->event_fd, EPOLL_CTL_DEL,
				  ev_fd->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		mm_event_receiver_add(return_events, MM_EVENT_UNREGISTER, ev_fd);
		break;

	default:
		ABORT();
	}
}

static void
mm_event_epoll_get_events(struct mm_event_epoll *event_backend,
			  struct mm_event_receiver *return_events,
			  int nevents)
{
	for (int i = 0; i < nevents; i++) {
		struct epoll_event *event = &event_backend->events[i];
		struct mm_event_fd *ev_fd = event->data.ptr;

		if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0)
			mm_event_receiver_add(return_events, MM_EVENT_INPUT_ERROR, ev_fd);
		else if ((event->events & EPOLLIN) != 0)
			mm_event_receiver_add(return_events, MM_EVENT_INPUT, ev_fd);
		if ((event->events & EPOLLOUT) != 0)
			mm_event_receiver_add(return_events, MM_EVENT_OUTPUT, ev_fd);
	}
}

static int
mm_event_epoll_poll(struct mm_event_epoll *event_backend, mm_timeout_t timeout)
{
	ENTER();

	// Find the event wait timeout.
	timeout /= 1000;

	// Publish the log before a possible sleep.
	if (timeout)
		mm_log_relay();

	// Poll the system for events.
	int n = mm_epoll_wait(event_backend->event_fd,
			      event_backend->events, MM_EVENT_EPOLL_NEVENTS,
			      timeout);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		n = 0;
	}

	LEAVE();
	return n;
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
	mm_close(event_backend->event_fd);

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_event_epoll_listen(struct mm_event_epoll *event_backend,
		      struct mm_event_batch *change_events,
		      struct mm_event_receiver *return_events,
		      mm_timeout_t timeout)
{
	ENTER();

	// Make event changes.
	for (unsigned int i = 0; i < change_events->nevents; i++) {
		struct mm_event *change_event = &change_events->events[i];
		mm_event_epoll_add_event(event_backend, change_event, return_events);
	}

	if (return_events != NULL) {
		// Poll for incoming events.
		int n = mm_event_epoll_poll(event_backend, timeout);

		// Store incoming events.
		mm_event_epoll_get_events(event_backend, return_events, n);
	}

	LEAVE();
}

#endif /* HAVE_SYS_EPOLL_H */
