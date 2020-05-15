/*
 * base/event/epoll.h - MainMemory epoll support.
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

#ifndef BASE_EVENT_EPOLL_H
#define BASE_EVENT_EPOLL_H

#include "common.h"

#if HAVE_SYS_EPOLL_H

#include "base/report.h"
#include "base/stdcall.h"
#include "base/event/event.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>

#define MM_EVENT_EPOLL_NEVENTS		(64)

/* Common data for epoll support. */
struct mm_event_epoll
{
	/* The epoll file descriptor. */
	int event_fd;

	/* The eventfd descriptor used for notification. */
	struct mm_event_fd notify_fd;
};

/* Per-listener data for epoll support. */
struct mm_event_epoll_local
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

/**********************************************************************
 * Event backend initialization and cleanup.
 **********************************************************************/

void NONNULL(1)
mm_event_epoll_prepare(struct mm_event_epoll *backend);

void NONNULL(1)
mm_event_epoll_cleanup(struct mm_event_epoll *backend);

void NONNULL(1)
mm_event_epoll_local_prepare(struct mm_event_epoll_local *local);

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
	return mm_syscall_4(SYS_epoll_wait, ep, (uintptr_t) events, nevents, timeout);
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
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_epoll_poll(struct mm_event_epoll *backend, struct mm_event_epoll_local *local, mm_timeout_t timeout);

void NONNULL(1)
mm_event_epoll_enable_notify(struct mm_event_epoll *backend);

void NONNULL(1)
mm_event_epoll_notify(struct mm_event_epoll *backend);

void NONNULL(1)
mm_event_epoll_notify_clean(struct mm_event_epoll *backend);

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_event_epoll_register_fd(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	uint32_t input = sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER);
	uint32_t output = sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER);
	if ((input | output) != 0) {
		struct epoll_event ee;
		ee.events = EPOLLET;
		if (input)
			ee.events |= EPOLLIN | EPOLLRDHUP;
		if (output)
			ee.events |= EPOLLOUT;
		ee.data.ptr = sink;

		int rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
	}
}

void NONNULL(1, 2, 3)
mm_event_epoll_unregister_fd(struct mm_event_epoll *backend, struct mm_event_epoll_local *local, struct mm_event_fd *sink);

static inline void NONNULL(1, 2)
mm_event_epoll_trigger_input(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	struct epoll_event ee;
	ee.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
	ee.data.ptr = sink;

	int rc;
	if ((sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER)) != 0) {
		ee.events |= EPOLLOUT;
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
	} else {
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
	}
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

static inline void NONNULL(1, 2)
mm_event_epoll_trigger_output(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	struct epoll_event ee;
	ee.events = EPOLLET | EPOLLOUT;
	ee.data.ptr = sink;

	int rc;
	if ((sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER)) != 0) {
		ee.events |= EPOLLIN | EPOLLRDHUP;
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
	} else {
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
	}
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

static void NONNULL(1, 2)
mm_event_epoll_disable_input(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	struct epoll_event ev;
	int rc;
	if ((sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER)) != 0) {
		ev.data.ptr = sink;
		ev.events = EPOLLET | EPOLLOUT;
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ev);
	} else {
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_DEL, sink->fd, &ev);
	}
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

static void NONNULL(1, 2)
mm_event_epoll_disable_output(struct mm_event_epoll *backend, struct mm_event_fd *sink)
{
	struct epoll_event ev;
	int rc;
	if ((sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER)) != 0) {
		ev.data.ptr = sink;
		ev.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ev);
	} else {
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_DEL, sink->fd, &ev);
	}
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

/**********************************************************************
 * Interface for handling events delivered to the target thread.
 **********************************************************************/

static inline void NONNULL(1)
mm_event_epoll_poller_start(struct mm_event_epoll_local *local)
{
	local->input_reset_num = 0;
	local->output_reset_num = 0;
}

static inline void NONNULL(1, 2)
mm_event_epoll_poller_finish(struct mm_event_epoll *backend, struct mm_event_epoll_local *local)
{
	for (int i = 0; i < local->input_reset_num; i++)
		mm_event_epoll_disable_input(backend, local->input_reset[i]);
	for (int i = 0; i < local->output_reset_num; i++)
		mm_event_epoll_disable_output(backend, local->output_reset[i]);
}

static inline void NONNULL(1, 2)
mm_event_epoll_poller_disable_input(struct mm_event_epoll_local *local, struct mm_event_fd *sink)
{
	ASSERT(local->input_reset_num < MM_EVENT_EPOLL_NEVENTS);
	local->input_reset[local->input_reset_num++] = sink;
}

static inline void NONNULL(1, 2)
mm_event_epoll_poller_disable_output(struct mm_event_epoll_local *local, struct mm_event_fd *sink)
{
	ASSERT(local->output_reset_num < MM_EVENT_EPOLL_NEVENTS);
	local->output_reset[local->output_reset_num++] = sink;
}

#endif /* HAVE_SYS_EPOLL_H */
#endif /* BASE_EVENT_EPOLL_H */
