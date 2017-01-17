/*
 * base/event/epoll.c - MainMemory epoll support.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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

#include "base/lock.h"
#include "base/logger.h"
#include "base/stdcall.h"
#include "base/event/batch.h"
#include "base/event/event.h"
#include "base/event/dispatch.h"
#include "base/event/nonblock.h"
#include "base/event/receiver.h"

#if HAVE_SYS_EVENTFD_H
# include <sys/eventfd.h>
#endif

#if MM_EVENT_NATIVE_NOTIFY

/* Event poll notification handler ID. */
static mm_event_hid_t mm_event_epoll_notify_handler;

static void
mm_event_epoll_handle_notify(mm_event_t event UNUSED, void *data)
{
	ENTER();

	struct mm_event_epoll *backend = containerof(data, struct mm_event_epoll, notify_fd);

	uint64_t value;
	int n = mm_read(backend->notify_fd.fd, &value, sizeof value);
	if (n != sizeof value)
		mm_warning(errno, "eventfd read");

	LEAVE();
}

#endif

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

void
mm_event_epoll_init(void)
{
	ENTER();

#if MM_EVENT_NATIVE_NOTIFY
	// Register the notify event handler.
	mm_event_epoll_notify_handler = mm_event_register_handler(mm_event_epoll_handle_notify);
#endif

	LEAVE();
}

static void
mm_event_epoll_add_change(struct mm_event_epoll *backend, struct mm_event_change *change,
			  struct mm_event_receiver *receiver)
{
	struct mm_event_fd *sink = change->sink;

	int rc;
	struct epoll_event ee;
	ee.data.ptr = sink;

	switch (change->kind) {
	case MM_EVENT_REGISTER:
		ee.events = EPOLLET;
		if (sink->regular_input || sink->oneshot_input)
			ee.events |= EPOLLIN | EPOLLRDHUP;
		if (sink->regular_output || sink->oneshot_output)
			ee.events |= EPOLLOUT;

		if (sink->oneshot_input)
			sink->oneshot_input_trigger = true;
		if (sink->oneshot_output)
			sink->oneshot_output_trigger = true;

		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");
		break;

	case MM_EVENT_UNREGISTER:
		rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_DEL, sink->fd, &ee);
		if (unlikely(rc < 0))
			mm_error(errno, "epoll_ctl");

		if (receiver != NULL)
			mm_event_receiver_unregister(receiver, sink);
		break;

	case MM_EVENT_TRIGGER_INPUT:
		// There is race condition here with access to trigger values. Another thread
		// might be clearing them concurrently (while setting should not be a problem
		// as only the owner thread is allowed to call the mm_event_trigger_input()
		// and mm_event_trigger_output() functions). But this should be recoverable.
		// See comments in the mm_event_epoll_receive_events() function.
		if (sink->oneshot_input && !sink->oneshot_input_trigger) {
			sink->oneshot_input_trigger = true;

			ee.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
			if (sink->regular_output || sink->oneshot_output_trigger)
				ee.events |= EPOLLOUT;

			if (sink->regular_output || sink->oneshot_output)
				rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
			else
				rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
			if (unlikely(rc < 0))
				mm_error(errno, "epoll_ctl");
		}
		break;

	case MM_EVENT_TRIGGER_OUTPUT:
		// There is race condition here with access to trigger values. Another thread
		// might be clearing them concurrently (while setting should not be a problem
		// as only the owner thread is allowed to call the mm_event_trigger_input()
		// and mm_event_trigger_output() functions). But this should be recoverable.
		// See comments in the mm_event_epoll_receive_events() function.
		if (sink->oneshot_output && !sink->oneshot_output_trigger) {
			sink->oneshot_output_trigger = true;

			ee.events = EPOLLET | EPOLLOUT;
			if (sink->regular_input || sink->oneshot_input_trigger)
				ee.events |= EPOLLIN | EPOLLRDHUP;

			if (sink->regular_input || sink->oneshot_input)
				rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
			else
				rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_ADD, sink->fd, &ee);
			if (unlikely(rc < 0))
				mm_error(errno, "epoll_ctl");
		}
		break;

	default:
		ABORT();
	}
}

static void
mm_event_epoll_receive_events(struct mm_event_epoll *backend,
			      struct mm_event_epoll_storage *storage,
			      struct mm_event_receiver *receiver,
			      int nevents)
{
	struct epoll_event ee;

	for (int i = 0; i < nevents; i++) {
		struct epoll_event *event = &storage->events[i];
		struct mm_event_fd *sink = event->data.ptr;

		if ((event->events & EPOLLIN) != 0) {
			if (!mm_event_receiver_adjust(receiver, sink))
				break;
		}
		if ((event->events & EPOLLOUT) != 0) {
			if (!mm_event_receiver_adjust(receiver, sink))
				break;
		}
	}

	bool locked = false;

	for (int i = 0; i < nevents; i++) {
		struct epoll_event *event = &storage->events[i];
		struct mm_event_fd *sink = event->data.ptr;

		if ((event->events & EPOLLIN) != 0) {
			if (sink->oneshot_input) {
				if (locked) {
					locked = false;
					mm_regular_unlock(&receiver->dispatch->event_sink_lock);
				}

				// TODO: If the event sink had both input and output in the
				// oneshot_trigger mode then there is a race condition here.
				// It looks harmless though. The event at hand is about to
				// cause some I/O which will discover the actual I/O state
				// and re-arm for it if needed. For the opposite I/O event
				// we keep it armed.
				// TODO: Delete it from epoll altogether if able to verify
				// that the opposite I/O event must be off too. However for
				// oneshot events this would take extra synchronization.

				int rc;
				if (sink->regular_output || sink->oneshot_output) {
					ee.data.ptr = sink;
					ee.events = EPOLLET | EPOLLOUT;
					rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
				} else {
					rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_DEL, sink->fd, &ee);
				}
				if (unlikely(rc < 0))
					mm_error(errno, "epoll_ctl");
			}

			if (!locked) {
				locked = true;
				mm_regular_lock(&receiver->dispatch->event_sink_lock);
			}

			sink->oneshot_input_trigger = false;
			mm_event_receiver_input(receiver, sink);
		}

		if ((event->events & EPOLLOUT) != 0) {
			if (sink->oneshot_output) {
				if (locked) {
					locked = false;
					mm_regular_unlock(&receiver->dispatch->event_sink_lock);
				}

				// TODO: If the event sink had both input and output in the
				// oneshot_trigger mode then there is a race condition here.
				// It looks harmless though. The event at hand is about to
				// cause some I/O which will discover the actual I/O state
				// and re-arm for it if needed. For the opposite I/O event
				// we keep it armed.
				// TODO: Delete it from epoll altogether if able to verify
				// that the opposite I/O event must be off too. However for
				// oneshot events this would take extra synchronization.

				int rc;
				if (sink->regular_input || sink->oneshot_input) {
					ee.data.ptr = sink;
					ee.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
					rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_MOD, sink->fd, &ee);
				} else {
					rc = mm_epoll_ctl(backend->event_fd, EPOLL_CTL_DEL, sink->fd, &ee);
				}
				if (unlikely(rc < 0))
					mm_error(errno, "epoll_ctl");
			}

			if (!locked) {
				locked = true;
				mm_regular_lock(&receiver->dispatch->event_sink_lock);
			}

			sink->oneshot_output_trigger = false;
			mm_event_receiver_output(receiver, sink);
		}

		if ((event->events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
			bool enable_input = sink->regular_input || sink->oneshot_input_trigger;
			bool enable_output = sink->regular_output || sink->oneshot_output_trigger;

			if (enable_input) {
				if (!locked) {
					locked = true;
					mm_regular_lock(&receiver->dispatch->event_sink_lock);
				}

				sink->oneshot_input_trigger = false;
				mm_event_receiver_input_error(receiver, sink);
			}

			if (enable_output && (event->events & (EPOLLERR | EPOLLHUP)) != 0) {
				if (!locked) {
					locked = true;
					mm_regular_lock(&receiver->dispatch->event_sink_lock);
				}

				sink->oneshot_output_trigger = false;
				mm_event_receiver_output_error(receiver, sink);
			}
		}
	}

	if (locked) {
		mm_regular_unlock(&receiver->dispatch->event_sink_lock);
	}
}

static int
mm_event_epoll_poll(struct mm_event_epoll *backend, struct mm_event_epoll_storage *storage,
		    mm_timeout_t timeout)
{
	ENTER();

	if (timeout) {
		// Calculate the event wait timeout.
		timeout /= 1000;

		// Publish the log before a possible sleep.
		mm_log_relay();
	}

	// Poll the system for events.
	int n = mm_epoll_wait(backend->event_fd, storage->events, MM_EVENT_EPOLL_NEVENTS,
			      timeout);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "epoll_wait");
		else
			mm_error(errno, "epoll_wait");
		n = 0;
	}
	storage->nevents_stats[n]++;

	LEAVE();
	return n;
}

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
mm_event_epoll_storage_prepare(struct mm_event_epoll_storage *storage)
{
	ENTER();

	for (size_t i = 0; i <= MM_EVENT_EPOLL_NEVENTS; i++)
		storage->nevents_stats[i] = 0;

	LEAVE();
}

void NONNULL(1, 2, 3)
mm_event_epoll_listen(struct mm_event_epoll *backend,
		      struct mm_event_batch *changes,
		      struct mm_event_receiver *receiver,
		      mm_timeout_t timeout)
{
	ENTER();
	struct mm_event_epoll_storage *storage = &receiver->storage.storage;

	// Make event changes.
	for (unsigned int i = 0; i < changes->nchanges; i++) {
		struct mm_event_change *change = &changes->changes[i];
		mm_event_epoll_add_change(backend, change, receiver);
	}

	// Poll for incoming events.
	int n = mm_event_epoll_poll(backend, storage, timeout);

	// Store incoming events.
	mm_event_epoll_receive_events(backend, storage, receiver, n);

	LEAVE();
}

void NONNULL(1, 2)
mm_event_epoll_change(struct mm_event_epoll *backend, struct mm_event_change *change)
{
	ENTER();

	mm_event_epoll_add_change(backend, change, NULL);

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
	mm_event_prepare_fd(&backend->notify_fd, fd, mm_event_epoll_notify_handler,
			    MM_EVENT_REGULAR, MM_EVENT_IGNORED, MM_EVENT_LOOSE);

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

#endif

#endif /* HAVE_SYS_EPOLL_H */
