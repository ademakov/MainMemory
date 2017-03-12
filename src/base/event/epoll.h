/*
 * base/event/epoll.h - MainMemory epoll support.
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

#ifndef BASE_EVENT_EPOLL_H
#define BASE_EVENT_EPOLL_H

#include "common.h"

#if HAVE_SYS_EPOLL_H

#include "base/report.h"
#include "base/stdcall.h"
#include "base/event/event.h"

#include <sys/epoll.h>

#if HAVE_SYS_EVENTFD_H
# define MM_EVENT_NATIVE_NOTIFY		1
#endif

#define MM_EVENT_EPOLL_NEVENTS		(64)

/* Common data for epoll support. */
struct mm_event_epoll
{
	/* The epoll file descriptor. */
	int event_fd;

#if MM_EVENT_NATIVE_NOTIFY
	/* The eventfd descriptor used for notification. */
	struct mm_event_fd notify_fd;
#endif
};

/* Per-listener data for epoll support. */
struct mm_event_epoll_storage
{
	/* The epoll list. */
	struct epoll_event events[MM_EVENT_EPOLL_NEVENTS];

 	/* Oneshot I/O reset buffers. */
	int input_reset_num;
	int output_reset_num;
	struct mm_event_fd *input_reset[MM_EVENT_EPOLL_NEVENTS];
	struct mm_event_fd *output_reset[MM_EVENT_EPOLL_NEVENTS];

#if ENABLE_EVENT_STATS
	/* Statistics. */
	uint64_t nevents_stats[MM_EVENT_EPOLL_NEVENTS + 1];
#endif
};

void NONNULL(1)
mm_event_epoll_prepare(struct mm_event_epoll *backend);

void NONNULL(1)
mm_event_epoll_cleanup(struct mm_event_epoll *backend);

void NONNULL(1)
mm_event_epoll_storage_prepare(struct mm_event_epoll_storage *storage);

/**********************************************************************
 * Wrappers for epoll system calls.
 **********************************************************************/

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

static inline int
mm_eventfd(unsigned int value, int flags)
{
	return mm_syscall_2(SYS_eventfd, value, flags);
}

#else

# define mm_epoll_create	epoll_create
# define mm_epoll_ctl		epoll_ctl
# define mm_epoll_wait		epoll_wait
# define mm_eventfd		eventfd

#endif

/**********************************************************************
 * Event polling and wakeup.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_epoll_listen(struct mm_event_epoll *backend, struct mm_event_epoll_storage *storage,
		      mm_timeout_t timeout);

#if MM_EVENT_NATIVE_NOTIFY

bool NONNULL(1)
mm_event_epoll_enable_notify(struct mm_event_epoll *backend);

void NONNULL(1)
mm_event_epoll_notify(struct mm_event_epoll *backend);

#endif

/**********************************************************************
 * I/O event control.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_event_epoll_register_fd(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	struct epoll_event ee;
	ee.events = EPOLLET;
	if (sink->regular_input || sink->oneshot_input)
		ee.events |= EPOLLIN | EPOLLRDHUP;
	if (sink->regular_output || sink->oneshot_output)
		ee.events |= EPOLLOUT;
	ee.data.ptr = sink;

	int rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

void NONNULL(1, 2, 3)
mm_event_epoll_unregister_fd(struct mm_event_epoll *backend, struct mm_event_epoll_storage *storage,
			     struct mm_event_fd *sink);

static inline void NONNULL(1, 2)
mm_event_epoll_trigger_input(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	struct epoll_event ee;
	ee.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
	if (sink->regular_output || sink->oneshot_output_trigger)
		ee.events |= EPOLLOUT;
	ee.data.ptr = sink;

	int rc;
	if (sink->regular_output || sink->oneshot_output)
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
	else
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

static inline void NONNULL(1, 2)
mm_event_epoll_trigger_output(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	struct epoll_event ee;
	ee.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
	if (sink->regular_output || sink->oneshot_output_trigger)
		ee.events |= EPOLLOUT;
	ee.data.ptr = sink;

	int rc;
	if (sink->regular_output || sink->oneshot_output)
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
	else
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

/**********************************************************************
 * I/O event processing.
 **********************************************************************/

void NONNULL(1)
mm_event_epoll_reset_input_low(struct mm_event_fd *sink);
void NONNULL(1)
mm_event_epoll_reset_output_low(struct mm_event_fd *sink);
void NONNULL(1, 2)
mm_event_epoll_reset_poller_input_low(struct mm_event_fd *sink, struct mm_event_listener *listener);
void NONNULL(1, 2)
mm_event_epoll_reset_poller_output_low(struct mm_event_fd *sink, struct mm_event_listener *listener);

static inline void NONNULL(1)
mm_event_epoll_reset_input(struct mm_event_fd *sink)
{
	if (sink->oneshot_input_trigger)
		mm_event_epoll_reset_input_low(sink);
}

static inline void NONNULL(1)
mm_event_epoll_reset_output(struct mm_event_fd *sink)
{
	if (sink->oneshot_output_trigger)
		mm_event_epoll_reset_output_low(sink);
}

static inline void NONNULL(1, 2)
mm_event_epoll_reset_poller_input(struct mm_event_fd *sink, struct mm_event_listener *listener)
{
	if (sink->oneshot_input_trigger)
		mm_event_epoll_reset_poller_input_low(sink, listener);
}

static inline void NONNULL(1, 2)
mm_event_epoll_reset_poller_output(struct mm_event_fd *sink, struct mm_event_listener *listener)
{
	if (sink->oneshot_output_trigger)
		mm_event_epoll_reset_poller_output_low(sink, listener);
}

#endif /* HAVE_SYS_EPOLL_H */
#endif /* BASE_EVENT_EPOLL_H */
