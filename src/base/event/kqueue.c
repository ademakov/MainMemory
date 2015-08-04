/*
 * base/event/kqueue.c - MainMemory kqueue support.
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

#include "base/event/kqueue.h"

#if HAVE_SYS_EVENT_H

#include "base/stdcall.h"
#include "base/event/batch.h"
#include "base/event/event.h"
#include "base/event/receiver.h"
#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/trace.h"

# include <time.h>

#if ENABLE_INLINE_SYSCALLS
static inline int
mm_kqueue(void)
{
	return mm_syscall_0(MM_SYSCALL_N(SYS_kqueue));
}

static inline int
mm_kevent(int kq, const struct kevent *changes, int nchanges,
	  struct kevent *events, int nevents, const struct timespec *ts)
{
	return mm_syscall_6(MM_SYSCALL_N(SYS_kevent),
			    kq, (uintptr_t) changes, nchanges,
			    (uintptr_t) events, nevents, (uintptr_t) ts);
}

#else

# define mm_kqueue	kqueue
# define mm_kevent	kevent

#endif

static bool
mm_event_kqueue_add_event(struct mm_event_kqueue *event_backend,
			  struct mm_event *change_event)
{
	int nevents = event_backend->nevents;
	struct mm_event_fd *ev_fd = change_event->ev_fd;

	switch (change_event->event) {
	case MM_EVENT_REGISTER:
		if (ev_fd->regular_input || ev_fd->oneshot_input) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(ev_fd->changed))
				return false;

			int flags;
			if (ev_fd->oneshot_input) {
				flags = EV_ADD | EV_ONESHOT;
				ev_fd->oneshot_input_trigger = 1;
			} else {
				flags = EV_ADD | EV_CLEAR;
			}

			struct kevent *kp = &event_backend->events[nevents++];
			EV_SET(kp, ev_fd->fd, EVFILT_READ, flags, 0, 0, ev_fd);
		}
		if (ev_fd->regular_output || ev_fd->oneshot_output) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(ev_fd->changed))
				return false;

			int flags;
			if (ev_fd->oneshot_output) {
				flags = EV_ADD | EV_ONESHOT;
				ev_fd->oneshot_output_trigger = 1;
			} else {
				flags = EV_ADD | EV_CLEAR;
			}

			struct kevent *kp = &event_backend->events[nevents++];
			EV_SET(kp, ev_fd->fd, EVFILT_WRITE, flags, 0, 0, ev_fd);
		}
		break;

	case MM_EVENT_UNREGISTER:
		if (ev_fd->regular_input || ev_fd->oneshot_input_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(ev_fd->changed))
				return false;

			struct kevent *kp = &event_backend->events[nevents++];
			EV_SET(kp, ev_fd->fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
		}
		if (ev_fd->regular_output || ev_fd->oneshot_output_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(ev_fd->changed))
				return false;

			struct kevent *kp = &event_backend->events[nevents++];
			EV_SET(kp, ev_fd->fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
		}
		break;

	case MM_EVENT_INPUT:
		if (ev_fd->oneshot_input && !ev_fd->oneshot_input_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(ev_fd->changed))
				return false;
			ev_fd->oneshot_input_trigger = 1;

			struct kevent *kp = &event_backend->events[nevents++];
			EV_SET(kp, ev_fd->fd, EVFILT_READ, EV_ADD | EV_ONESHOT,
			       0, 0, ev_fd);
		}
		break;

	case MM_EVENT_OUTPUT:
		if (ev_fd->oneshot_output && !ev_fd->oneshot_output_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(ev_fd->changed))
				return false;
			ev_fd->oneshot_output_trigger = 1;

			struct kevent *kp = &event_backend->events[nevents++];
			EV_SET(kp, ev_fd->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT,
			       0, 0, ev_fd);
		}
		break;

	default:
		ABORT();
	}

	if (event_backend->nevents != nevents) {
		event_backend->nevents = nevents;
		ev_fd->changed = 1;
	}

	return true;
}

static void
mm_event_kqueue_get_incoming_events(struct mm_event_kqueue *event_backend,
				    struct mm_event_receiver *return_events,
				    int nevents)
{
	for (int i = 0; i < nevents; i++) {
		struct kevent *event = &event_backend->events[i];

		if (event->filter == EVFILT_READ) {
			DEBUG("read event");

			struct mm_event_fd *ev_fd = event->udata;
			ev_fd->oneshot_input_trigger = 0;

			if ((event->flags & (EV_ERROR | EV_EOF)) != 0)
				mm_event_receiver_add(return_events, MM_EVENT_INPUT_ERROR, ev_fd);
			else
				mm_event_receiver_add(return_events, MM_EVENT_INPUT, ev_fd);

		} else if (event->filter == EVFILT_WRITE) {
			DEBUG("write event");

			struct mm_event_fd *ev_fd = event->udata;
			ev_fd->oneshot_output_trigger = 0;

			if ((event->flags & (EV_ERROR | EV_EOF)) != 0)
				mm_event_receiver_add(return_events, MM_EVENT_OUTPUT_ERROR, ev_fd);
			else
				mm_event_receiver_add(return_events, MM_EVENT_OUTPUT, ev_fd);
		}
	}
}

static void
mm_event_kqueue_get_unregister_events(struct mm_event_receiver *return_events,
				      struct mm_event_batch *change_events,
				      unsigned int first, unsigned int last)
{
	for (unsigned int i = first; i < last; i++) {
		struct mm_event *event = &change_events->events[i];
		struct mm_event_fd *ev_fd = event->ev_fd;

		// Reset the change flag.
		ev_fd->changed = 0;

		// Store the pertinent event.
		if (event->event == MM_EVENT_UNREGISTER)
			mm_event_receiver_add(return_events, MM_EVENT_UNREGISTER, ev_fd);
	}
}

static void
mm_event_kqueue_change(struct mm_event_kqueue *event_backend)
{
	ENTER();

	// Submit change events.
	int n = mm_kevent(event_backend->event_fd,
			  event_backend->events, event_backend->nevents,
			  NULL, 0, NULL);
	DEBUG("kevent changed: %d, received: %d", event_backend->nevents, n);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "kevent");
		else
			mm_error(errno, "kevent");
	}
	event_backend->nevents = 0;

	LEAVE();
}

static int
mm_event_kqueue_poll(struct mm_event_kqueue *event_backend, mm_timeout_t timeout)
{
	ENTER();
	DEBUG("poll: changes: %d, timeout: %lu", event_backend->nevents, (unsigned long) timeout);

	// Calculate the event wait timeout.
	struct timespec ts;
	ts.tv_sec = timeout / 1000000;
	ts.tv_nsec = (timeout % 1000000) * 1000;

	// Publish the log before a possible sleep.
	if (timeout)
		mm_log_relay();

	// Poll the system for events.
	int n = mm_kevent(event_backend->event_fd,
			  event_backend->events, event_backend->nevents,
			  event_backend->events, MM_EVENT_KQUEUE_NEVENTS,
			  &ts);
	DEBUG("kevent changed: %d, received: %d", event_backend->nevents, n);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "kevent");
		else
			mm_error(errno, "kevent");
		n = 0;
	}
	event_backend->nevents = 0;

	LEAVE();
	return n;
}

void __attribute__((nonnull(1)))
mm_event_kqueue_prepare(struct mm_event_kqueue *event_backend)
{
	ENTER();

	// Open a kqueue file descriptor.
	event_backend->event_fd = mm_kqueue();
	if (event_backend->event_fd == -1)
		mm_fatal(errno, "Failed to create kqueue");

	event_backend->nevents = 0;

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_kqueue_cleanup(struct mm_event_kqueue *event_backend)
{
	ENTER();

	// Close the kqueue file descriptor.
	mm_close(event_backend->event_fd);

	LEAVE();
}

void __attribute__((nonnull(1, 2)))
mm_event_kqueue_listen(struct mm_event_kqueue *event_backend,
		       struct mm_event_batch *change_events,
		       struct mm_event_receiver *return_events,
		       mm_timeout_t timeout)
{
	ENTER();

	// Make event changes.
	unsigned int first = 0, next = 0;
	while (next < change_events->nevents) {
		struct mm_event *event = &change_events->events[next];
		if (likely(mm_event_kqueue_add_event(event_backend, event))) {
			// Proceed with more change events if any.
			next++;
		} else {
			// Flush event changes.
			mm_event_kqueue_change(event_backend);

			// Store unregister events.
			mm_event_kqueue_get_unregister_events(return_events,
							      change_events,
							      first, next);

			// Proceed with more change events if any.
			first = next;
			continue;
		}
	}

	if (return_events != NULL) {
		// Poll for incoming events.
		int n = mm_event_kqueue_poll(event_backend, timeout);

		// Store incoming events.
		mm_event_kqueue_get_incoming_events(event_backend,
						    return_events, n);

		// Store unregister events.
		mm_event_kqueue_get_unregister_events(return_events,
						      change_events,
						      first,
						      change_events->nevents);
	} else {
		// Flush event changes.
		mm_event_kqueue_change(event_backend);
	}

	LEAVE();
}

#endif /* HAVE_SYS_EVENT_H */
