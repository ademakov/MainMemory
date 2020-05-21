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

#include "base/report.h"
#include "base/stdcall.h"
#include "base/event/event.h"
#include "base/event/dispatch.h"
#include "base/event/nonblock.h"
#include "base/event/listener.h"

#include <sys/eventfd.h>

#define MM_EVENT_EPOLL_NOTIFY_FD ((struct mm_event_fd *) -1)
#define MM_EVENT_EPOLL_COMMON_FD ((struct mm_event_fd *) -2)

/**********************************************************************
 * Wrappers for epoll system calls.
 **********************************************************************/

#if ENABLE_INLINE_SYSCALLS

static inline int
mm_epoll_create(int flags)
{
	return mm_syscall_1(SYS_epoll_create1, flags);
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
 * Helper routines.
 **********************************************************************/

static void
mm_event_epoll_add_fd(struct mm_event_epoll *backend, int fd, uint32_t events, void *ptr)
{
	struct epoll_event ee;
	ee.events = events;
	ee.data.ptr = ptr;
	int rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, fd, &ee);
	if (unlikely(rc < 0))
		mm_fatal(errno, "epoll_ctl");
}

static void NONNULL(1, 2, 3)
mm_event_epoll_acquire_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink, uint32_t events)
{
	int rc;

	if ((sink->flags & MM_EVENT_COMMON_ENABLED) != 0) {
		sink->flags &= ~(MM_EVENT_COMMON_ADDED | MM_EVENT_COMMON_ENABLED);
		rc = mm_epoll_ctl(common->event_fd, EPOLL_CTL_DEL, sink->fd, NULL);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
	}

	sink->flags &= ~MM_EVENT_LOCAL_MODIFY;
	struct epoll_event ee = { .events = events, .data.ptr = sink };
	if ((sink->flags & MM_EVENT_LOCAL_ADDED) != 0) {
		rc = mm_epoll_ctl(local->poll.event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
	} else {
		sink->flags |= MM_EVENT_LOCAL_ADDED;
		rc = mm_epoll_ctl(local->poll.event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
	}
	if (unlikely(rc < 0))
		mm_error(errno, "epoll_ctl");
}

static void
mm_event_epoll_handle(struct mm_event_listener *listener, struct mm_event_epoll *common, int nevents)
{
	bool do_common_poll = false;

	mm_event_listener_handle_start(listener, nevents);

	for (int i = 0; i < nevents; i++) {
		struct epoll_event *event = &listener->backend.events[i];
		struct mm_event_fd *sink = event->data.ptr;

		if ((event->events & EPOLLIN) != 0) {
			if (sink == MM_EVENT_EPOLL_COMMON_FD) {
				do_common_poll = true;
			} else if (sink == MM_EVENT_EPOLL_NOTIFY_FD) {
				listener->backend.notified = true;
				listener->notifications++;
			} else {
				if ((sink->flags & MM_EVENT_INPUT_TRIGGER) != 0) {
					sink->flags &= ~MM_EVENT_INPUT_TRIGGER;
					//sink->flags |= MM_EVENT_LOCAL_MODIFY;
				}
				mm_event_listener_input(listener, sink);
			}
		}

		if ((event->events & EPOLLOUT) != 0) {
			if ((sink->flags & MM_EVENT_OUTPUT_TRIGGER) != 0) {
				sink->flags &= ~MM_EVENT_OUTPUT_TRIGGER;
				//sink->flags |= MM_EVENT_LOCAL_MODIFY;
			}
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

	if (do_common_poll) {
		nevents = mm_epoll_wait(common->event_fd, listener->backend.events, MM_EVENT_EPOLL_NEVENTS, 0);
		if (unlikely(nevents < 0)) {
			if (errno == EINTR)
				mm_warning(errno, "epoll_wait");
			else
				mm_error(errno, "epoll_wait");
			nevents = 0;
		}

		for (int i = 0; i < nevents; i++) {
			struct epoll_event *event = &listener->backend.events[i];
			struct mm_event_fd *sink = event->data.ptr;

			sink->flags &= ~MM_EVENT_COMMON_ENABLED;

			if ((event->events & EPOLLIN) != 0)
				mm_event_listener_input(listener, sink);
			if ((event->events & EPOLLOUT) != 0)
				mm_event_listener_output(listener, sink);

			if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
				if ((sink->flags & MM_EVENT_REGULAR_INPUT) != 0)
					mm_event_listener_input_error(listener, sink);
				if ((sink->flags & MM_EVENT_REGULAR_OUTPUT) != 0 && (event->events & (EPOLLERR | EPOLLHUP)) != 0)
					mm_event_listener_output_error(listener, sink);
			}
		}

#if ENABLE_EVENT_STATS
		listener->backend.nevents_stats[nevents]++;
#endif
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
	backend->event_fd = mm_epoll_create(EPOLL_CLOEXEC);
	if (backend->event_fd < 0)
		mm_fatal(errno, "failed to create epoll fd");

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_cleanup(struct mm_event_epoll *backend)
{
	ENTER();

	// Close the epoll file descriptor.
	mm_close(backend->event_fd);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_epoll_local_prepare(struct mm_event_epoll_local *local, struct mm_event_epoll *common)
{
	ENTER();

	// Prepare the local epoll instance.
	mm_event_epoll_prepare(&local->poll);

	// Register the common epoll instance with the local epoll instance.
	mm_event_epoll_add_fd(&local->poll, common->event_fd, EPOLLIN | EPOLLET, MM_EVENT_EPOLL_COMMON_FD);

	// Create a file descriptor for notifications.
	local->notify_fd = mm_eventfd(0, 0);
	if (local->notify_fd < 0)
		mm_fatal(errno, "eventfd");
	// Set it up for non-blocking I/O.
	mm_set_nonblocking(local->notify_fd);
	// Register the file descriptor.
	mm_event_epoll_add_fd(&local->poll, local->notify_fd, EPOLLIN | EPOLLET, MM_EVENT_EPOLL_NOTIFY_FD);

	// Clean the notification flag.
	local->notified = false;

#if ENABLE_EVENT_STATS
	for (size_t i = 0; i <= MM_EVENT_EPOLL_NEVENTS; i++)
		local->nevents_stats[i] = 0;
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_local_cleanup(struct mm_event_epoll_local *local)
{
	// Close the eventfd file descriptor.
	if (local->notify_fd >= 0)
		mm_close(local->notify_fd);

	mm_event_epoll_cleanup(&local->poll);
}

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_epoll_poll(struct mm_event_epoll_local *local, struct mm_event_epoll *backend, mm_timeout_t timeout)
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
	int n = mm_epoll_wait(local->poll.event_fd, local->events, MM_EVENT_EPOLL_NEVENTS, timeout);
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
		mm_event_epoll_handle(listener, backend, n);
	}

#if ENABLE_EVENT_STATS
	local->nevents_stats[n]++;
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_notify(struct mm_event_epoll_local *local)
{
	ENTER();

	uint64_t value = 1;
	int n = mm_write(local->notify_fd, &value, sizeof value);
	if (unlikely(n != sizeof value))
		mm_fatal(errno, "eventfd write");

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_notify_clean(struct mm_event_epoll_local *local)
{
	ENTER();

	if (local->notified) {
		local->notified = false;

		uint64_t value;
		int n = mm_read(local->notify_fd, &value, sizeof value);
		if (n != sizeof value)
			mm_warning(errno, "eventfd read");
	}

	LEAVE();
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_epoll_register_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink)
{
	uint32_t input = sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER);
	uint32_t output = sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER);
	if ((input | output) != 0) {
		int events = EPOLLET;
		if (input)
			events |= EPOLLIN | EPOLLRDHUP;
		if (output)
			events |= EPOLLOUT;

		if ((sink->flags & (MM_EVENT_INPUT_TRIGGER | MM_EVENT_OUTPUT_TRIGGER | MM_EVENT_PINNED_LOCAL)) == 0) {
			sink->flags |= MM_EVENT_COMMON_ADDED | MM_EVENT_COMMON_ENABLED;
			events |= EPOLLONESHOT;
		} else {
			sink->flags |= MM_EVENT_LOCAL_ADDED;
			common = &local->poll;
		}

		struct epoll_event ee = { .events = events, .data.ptr = sink };
		int rc = mm_epoll_ctl(common->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
	}
}

void NONNULL(1, 2, 3)
mm_event_epoll_unregister_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink)
{
	ENTER();

	struct mm_event_listener *listener = containerof(local, struct mm_event_listener, backend);
	struct mm_event_dispatch *dispatch = containerof(common, struct mm_event_dispatch, backend);

	// Start a reclamation epoch.
	mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	// Delete the file descriptor from epoll.
	if ((sink->flags & MM_EVENT_LOCAL_ADDED) != 0) {
		int rc = mm_epoll_ctl(local->poll.event_fd, EPOLL_CTL_DEL, sink->fd, NULL);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
	}
	if ((sink->flags & MM_EVENT_COMMON_ADDED) != 0) {
		int rc = mm_epoll_ctl(common->event_fd, EPOLL_CTL_DEL, sink->fd, NULL);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
	}

	// Finish unregister call sequence.
	mm_event_listener_unregister(listener, sink);

	// Attempt to advance the reclamation epoch and possibly finish it.
	mm_event_epoch_advance(&listener->epoch, &dispatch->global_epoch);

	LEAVE();
}

void NONNULL(1, 2, 3)
mm_event_epoll_trigger_input(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink)
{
	uint32_t events = EPOLLET | EPOLLIN | EPOLLRDHUP;
	if ((sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER)) != 0)
		events |= EPOLLOUT;
	mm_event_epoll_acquire_fd(local, common, sink, events);
}

void NONNULL(1, 2, 3)
mm_event_epoll_trigger_output(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink)
{
	uint32_t events = EPOLLET | EPOLLOUT;
	if ((sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER)) != 0)
		events |= EPOLLIN | EPOLLRDHUP;
	mm_event_epoll_acquire_fd(local, common, sink, events);
}

void NONNULL(1, 2, 3)
mm_event_epoll_adjust_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink)
{
	if ((sink->flags & (MM_EVENT_LOCAL_ADDED | MM_EVENT_LOCAL_MODIFY)) != MM_EVENT_LOCAL_ADDED) {
		uint32_t input = sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER);
		uint32_t output = sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER);
		if ((input | output) != 0) {
			int events = EPOLLET;
			if (input)
				events |= EPOLLIN | EPOLLRDHUP;
			if (output)
				events |= EPOLLOUT;
			mm_event_epoll_acquire_fd(local, common, sink, events);
		}
	}
}

void NONNULL(1, 2, 3)
mm_event_epoll_disable_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common UNUSED, struct mm_event_fd *sink)
{
	if ((sink->flags & MM_EVENT_LOCAL_ADDED) != 0) {
		int rc = mm_epoll_ctl(local->poll.event_fd, EPOLL_CTL_DEL, sink->fd, NULL);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
		sink->flags &= ~(MM_EVENT_LOCAL_ADDED | MM_EVENT_LOCAL_MODIFY);
	}
}

void NONNULL(1, 2, 3)
mm_event_epoll_dispose_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink)
{
	mm_event_epoll_disable_fd(local, common, sink);

	uint32_t input = sink->flags & MM_EVENT_REGULAR_INPUT;
	uint32_t output = sink->flags & MM_EVENT_REGULAR_OUTPUT;
	if ((input | output) != 0) {
		int events = EPOLLET | EPOLLONESHOT;
		if (input)
			events |= EPOLLIN | EPOLLRDHUP;
		if (output)
			events |= EPOLLOUT;

		int rc;
		struct epoll_event ee = { .events = events, .data.ptr = sink };
		if ((sink->flags & MM_EVENT_COMMON_ADDED) != 0)
			rc = mm_epoll_ctl(common->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
		else
			rc = mm_epoll_ctl(common->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		sink->flags |= MM_EVENT_COMMON_ADDED | MM_EVENT_COMMON_ENABLED;
	}
}

#endif /* HAVE_SYS_EPOLL_H */
