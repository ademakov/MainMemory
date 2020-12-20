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
#include "base/memory/alloc.h"

#include <sys/eventfd.h>

#define MM_EVENT_EPOLL_NOTIFY_FD ((struct mm_event_fd *) -1)

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
mm_event_epoll_stash_event(struct mm_event_epoll_local *local, struct mm_event_fd *sink, uint32_t flags)
{
	if (local->stash_size >= local->stash_capacity) {
		if (unlikely(local->stash_capacity == UINT32_MAX))
			mm_fatal(0, "too large epoll event stash");

		local->stash_capacity *= 2;
		local->stash = ((struct mm_event_epoll_stash *)
				mm_memory_xrealloc(
					local->stash,
					local->stash_capacity * sizeof(struct mm_event_epoll_stash)));
	}

	local->stash[local->stash_size++] = (struct mm_event_epoll_stash) { sink, flags };
}

static bool
mm_event_epoll_ctl(int ep, int op, int fd, struct epoll_event *event)
{
	if (unlikely(mm_epoll_ctl(ep, op, fd, event) < 0)) {
		mm_error(errno, "epoll_ctl");
		return false;
	}
	return true;
}

static bool
mm_event_epoll_add(int ep, int fd, uint32_t events, void *ptr)
{
	struct epoll_event ee = { .events = events, .data.ptr = ptr };
	return mm_event_epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ee);
}

static bool
mm_event_epoll_ctl_sink(int ep, int op, struct mm_event_fd *sink, uint32_t events)
{
	struct epoll_event ee = { .events = events, .data.ptr = sink };
	return mm_event_epoll_ctl(ep, op, sink->fd, &ee);
}

static void
mm_event_epoll_handle(struct mm_event_listener *const listener, struct mm_event_epoll *const common, const int nevents)
{
	for (int i = 0; i < nevents; i++) {
		struct epoll_event *const event = &listener->backend.events[i];
		struct mm_event_fd *const sink = event->data.ptr;

		if (sink == MM_EVENT_EPOLL_NOTIFY_FD) {
			common->notified = true;
			listener->notifications++;
			continue;
		}

		const uint32_t ev = event->events;
		if ((ev & EPOLLIN) != 0)
			mm_event_listener_input(listener, sink, MM_EVENT_INPUT_READY);
		if ((ev & EPOLLOUT) != 0)
			mm_event_listener_output(listener, sink, MM_EVENT_OUTPUT_READY);

		if ((ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
			const uint32_t flags = sink->flags;
			if ((flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_ONESHOT_INPUT)) != 0)
				mm_event_listener_input(listener, sink, MM_EVENT_INPUT_ERROR);
			if ((flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_ONESHOT_OUTPUT)) != 0 && (ev & (EPOLLERR | EPOLLHUP)) != 0)
				mm_event_listener_output(listener, sink, MM_EVENT_OUTPUT_ERROR);
		}
	}
}

/**********************************************************************
 * Event backend initialization and cleanup.
 **********************************************************************/

void NONNULL(1)
mm_event_epoll_prepare(struct mm_event_epoll *common)
{
	ENTER();

	// Open an epoll file descriptor.
	common->event_fd = mm_epoll_create(EPOLL_CLOEXEC);
	if (common->event_fd < 0)
		mm_fatal(errno, "failed to create epoll fd");

	// Create a file descriptor for notifications.
	common->notify_fd = mm_eventfd(0, 0);
	if (common->notify_fd < 0)
		mm_fatal(errno, "eventfd");
	// Set it up for non-blocking I/O.
	mm_set_nonblocking(common->notify_fd);
	// Register the file descriptor.
	if (!mm_event_epoll_add(common->event_fd, common->notify_fd, EPOLLIN | EPOLLET, MM_EVENT_EPOLL_NOTIFY_FD))
		mm_fatal(errno, "failed to register event fd");

	// Clean the notification flag.
	common->notified = false;

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_cleanup(struct mm_event_epoll *common)
{
	ENTER();

	// Close the epoll file descriptor.
	mm_close(common->event_fd);

	// Close the eventfd file descriptor.
	if (common->notify_fd >= 0)
		mm_close(common->notify_fd);

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_local_prepare(struct mm_event_epoll_local *local)
{
	ENTER();

	// Initialize the events stash.
	local->stash_size = 0;
	local->stash_capacity = 10;
	local->stash = ((struct mm_event_epoll_stash *)
			mm_memory_xalloc(local->stash_capacity * sizeof(struct mm_event_epoll_stash)));

#if ENABLE_EVENT_STATS
	for (size_t i = 0; i <= MM_EVENT_EPOLL_NEVENTS; i++)
		local->nevents_stats[i] = 0;
#endif

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_local_cleanup(struct mm_event_epoll_local *local)
{
	// Free the events stash.
	mm_memory_free(local->stash);
}

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_epoll_poll(struct mm_event_epoll *common, struct mm_event_epoll_local *local, mm_timeout_t timeout)
{
	ENTER();
	DEBUG("timeout=%u", timeout);
	struct mm_event_listener *const listener = containerof(local, struct mm_event_listener, backend);

	// Handle deferred errors.
	const uint32_t stash_size = local->stash_size;
	if (stash_size != 0) {
		local->stash_size = 0;
		for (uint32_t i = 0; i < stash_size; i++) {
			const uint32_t flags = local->stash[i].flags;
			switch (flags) {
			case MM_EVENT_INPUT_ERROR:
				mm_event_listener_input(listener, local->stash[i].sink, flags);
				break;
			case MM_EVENT_OUTPUT_ERROR:
				mm_event_listener_output(listener, local->stash[i].sink, flags);
				break;
			}
		}
	}

	if (timeout) {
		// Announce that the thread is about to sleep.
		mm_stamp_t stamp = mm_event_listener_polling(listener);
		if (!mm_event_listener_restful(listener, stamp)) {
			timeout = 0;
		} else {
			// Calculate the event wait timeout.
			timeout = (timeout + 1000 - 1) / 1000;
		}
	}

	// Poll the system for events.
	int n = mm_epoll_wait(common->event_fd, local->events, MM_EVENT_EPOLL_NEVENTS, timeout);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		n = 0;
	}
#if ENABLE_EVENT_STATS
	local->nevents_stats[n]++;
#endif

	// Announce the start of another working cycle.
	mm_event_listener_running(listener);

	// Handle incoming events.
	if (n != 0) {
		mm_event_epoll_handle(listener, common, n);
		mm_event_listener_flush(listener);
	}

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_notify(struct mm_event_epoll *common)
{
	ENTER();

	uint64_t value = 1;
	int n = mm_write(common->notify_fd, &value, sizeof value);
	if (unlikely(n != sizeof value))
		mm_fatal(errno, "eventfd write");

	LEAVE();
}

void NONNULL(1)
mm_event_epoll_notify_clean(struct mm_event_epoll *common)
{
	ENTER();

	if (common->notified) {
		common->notified = false;

		uint64_t value;
		int n = mm_read(common->notify_fd, &value, sizeof value);
		if (n != sizeof value)
			mm_warning(errno, "eventfd read");
	}

	LEAVE();
}

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_epoll_register_fd(struct mm_event_epoll *common, struct mm_event_epoll_local *local, struct mm_event_fd *sink)
{
	const uint32_t flags = sink->flags;
	const uint32_t input = flags & MM_EVENT_REGULAR_INPUT;
	const uint32_t output = flags & MM_EVENT_REGULAR_OUTPUT;
	if ((input | output) == 0)
		return;

	const int events = input ? EPOLLET | EPOLLIN | EPOLLRDHUP : EPOLLET | EPOLLOUT;
	if (!mm_event_epoll_ctl_sink(common->event_fd, EPOLL_CTL_ADD, sink, events))
		mm_event_epoll_stash_event(local, sink, input ? MM_EVENT_INPUT_ERROR : MM_EVENT_OUTPUT_ERROR);
}

void NONNULL(1, 2, 3)
mm_event_epoll_unregister_fd(struct mm_event_epoll *common, struct mm_event_epoll_local *local, struct mm_event_fd *sink)
{
	struct mm_event_listener *listener = containerof(local, struct mm_event_listener, backend);
	struct mm_event_dispatch *dispatch = containerof(common, struct mm_event_dispatch, backend);

	// Start a reclamation epoch.
	mm_event_epoch_enter(&listener->epoch, &dispatch->global_epoch);

	// Delete the file descriptor from epoll.
	const uint32_t flags = sink->flags;
	if ((flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_REGULAR_OUTPUT | MM_EVENT_ONESHOT_INPUT | MM_EVENT_ONESHOT_OUTPUT)) != 0)
		mm_event_epoll_ctl_sink(common->event_fd, EPOLL_CTL_DEL, sink, 0);

	// Finish unregister call sequence.
	mm_event_listener_unregister(listener, sink);

	// Attempt to advance the reclamation epoch and possibly finish it.
	mm_event_epoch_advance(&listener->epoch, &dispatch->global_epoch);
}

void NONNULL(1, 2, 3)
mm_event_epoll_enable_input(struct mm_event_epoll *common, struct mm_event_epoll_local *const local, struct mm_event_fd *const sink)
{
	int op = EPOLL_CTL_ADD;
	uint32_t events = EPOLLET | EPOLLIN | EPOLLRDHUP;
	if ((sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_ONESHOT_OUTPUT)) != 0) {
		op = EPOLL_CTL_MOD;
		events |= EPOLLOUT;
	}

	if (!mm_event_epoll_ctl_sink(common->event_fd, op, sink, events))
		mm_event_epoll_stash_event(local, sink, MM_EVENT_INPUT_ERROR);
}

void NONNULL(1, 2, 3)
mm_event_epoll_enable_output(struct mm_event_epoll *common, struct mm_event_epoll_local *const local, struct mm_event_fd *const sink)
{
	int op = EPOLL_CTL_ADD;
	uint32_t events = EPOLLET | EPOLLOUT;
	if ((sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_ONESHOT_INPUT)) != 0) {
		op = EPOLL_CTL_MOD;
		events |= EPOLLIN | EPOLLRDHUP;
	}

	if (!mm_event_epoll_ctl_sink(common->event_fd, op, sink, events))
		mm_event_epoll_stash_event(local, sink, MM_EVENT_OUTPUT_ERROR);
}

void NONNULL(1, 2, 3)
mm_event_epoll_disable_input(struct mm_event_epoll *common, struct mm_event_epoll_local *const local, struct mm_event_fd *const sink)
{
	int op = EPOLL_CTL_DEL;
	uint32_t events = 0;
	if ((sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_ONESHOT_OUTPUT)) != 0) {
		op = EPOLL_CTL_MOD;
		events = EPOLLET | EPOLLOUT;
	}

	if (!mm_event_epoll_ctl_sink(common->event_fd, op, sink, events))
		mm_event_epoll_stash_event(local, sink, MM_EVENT_INPUT_ERROR);
}

void NONNULL(1, 2, 3)
mm_event_epoll_disable_output(struct mm_event_epoll *common, struct mm_event_epoll_local *const local, struct mm_event_fd *const sink)
{
	int op = EPOLL_CTL_DEL;
	uint32_t events = 0;
	if ((sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_ONESHOT_INPUT)) != 0) {
		op = EPOLL_CTL_MOD;
		events = EPOLLET | EPOLLIN | EPOLLRDHUP;
	}

	if (!mm_event_epoll_ctl_sink(common->event_fd, op, sink, events))
		mm_event_epoll_stash_event(local, sink, MM_EVENT_OUTPUT_ERROR);
}

#endif /* HAVE_SYS_EPOLL_H */
