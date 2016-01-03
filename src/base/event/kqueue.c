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

#include <time.h>

#define MM_EVENT_KQUEUE_NOTIFY_ID	123

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
mm_event_kqueue_add_change(struct mm_event_kqueue_storage *storage,
			   struct mm_event_change *change)
{
	int nevents = storage->nevents;
	struct mm_event_fd *sink = change->sink;

	switch (change->kind) {
	case MM_EVENT_REGISTER:
		if (sink->regular_input || sink->oneshot_input) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(sink->changed))
				return false;

			int flags;
			if (sink->oneshot_input) {
				flags = EV_ADD | EV_ONESHOT;
				sink->oneshot_input_trigger = 1;
			} else {
				flags = EV_ADD | EV_CLEAR;
			}

			struct kevent *kp = &storage->events[nevents++];
			EV_SET(kp, sink->fd, EVFILT_READ, flags, 0, 0, sink);
		}
		if (sink->regular_output || sink->oneshot_output) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(sink->changed))
				return false;

			int flags;
			if (sink->oneshot_output) {
				flags = EV_ADD | EV_ONESHOT;
				sink->oneshot_output_trigger = 1;
			} else {
				flags = EV_ADD | EV_CLEAR;
			}

			struct kevent *kp = &storage->events[nevents++];
			EV_SET(kp, sink->fd, EVFILT_WRITE, flags, 0, 0, sink);
		}
		break;

	case MM_EVENT_UNREGISTER:
		if (sink->regular_input || sink->oneshot_input_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(sink->changed))
				return false;

			struct kevent *kp = &storage->events[nevents++];
			EV_SET(kp, sink->fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
		}
		if (sink->regular_output || sink->oneshot_output_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(sink->changed))
				return false;

			struct kevent *kp = &storage->events[nevents++];
			EV_SET(kp, sink->fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
		}
		break;

	case MM_EVENT_TRIGGER_INPUT:
		if (sink->oneshot_input && !sink->oneshot_input_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(sink->changed))
				return false;
			sink->oneshot_input_trigger = 1;

			struct kevent *kp = &storage->events[nevents++];
			EV_SET(kp, sink->fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, sink);
		}
		break;

	case MM_EVENT_TRIGGER_OUTPUT:
		if (sink->oneshot_output && !sink->oneshot_output_trigger) {
			if (unlikely(nevents == MM_EVENT_KQUEUE_NEVENTS))
				return false;
			if (unlikely(sink->changed))
				return false;
			sink->oneshot_output_trigger = 1;

			struct kevent *kp = &storage->events[nevents++];
			EV_SET(kp, sink->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, sink);
		}
		break;

	default:
		ABORT();
	}

	if (storage->nevents != nevents) {
		storage->nevents = nevents;
		sink->changed = 1;
	}

	return true;
}

static void
mm_event_kqueue_receive_events(struct mm_event_kqueue_storage *storage,
			       struct mm_event_receiver *receiver,
			       int nevents)
{
	for (int i = 0; i < nevents; i++) {
		struct kevent *event = &storage->events[i];

		if (event->filter == EVFILT_READ) {
			DEBUG("read event");

			struct mm_event_fd *sink = event->udata;
			if ((event->flags & (EV_ERROR | EV_EOF)) != 0)
				mm_event_receiver_input_error(receiver, sink);
			else
				mm_event_receiver_input(receiver, sink);

		} else if (event->filter == EVFILT_WRITE) {
			DEBUG("write event");

			struct mm_event_fd *sink = event->udata;
			if ((event->flags & (EV_ERROR | EV_EOF)) != 0)
				mm_event_receiver_output_error(receiver, sink);
			else
				mm_event_receiver_output(receiver, sink);

		} else if (event->filter == EVFILT_USER) {
			ASSERT(event->ident == MM_EVENT_KQUEUE_NOTIFY_ID);
		}
	}
}

static void
mm_event_kqueue_postprocess_changes(struct mm_event_batch *changes,
				    struct mm_event_receiver *receiver,
				    unsigned int first, unsigned int last)
{
	if (receiver != NULL) {
		for (unsigned int i = first; i < last; i++) {
			struct mm_event_change *change = &changes->changes[i];
			struct mm_event_fd *sink = change->sink;

			// Reset the change flag.
			sink->changed = 0;

			// Store the pertinent event.
			if (change->kind == MM_EVENT_UNREGISTER)
				mm_event_receiver_unregister(receiver, sink);
		}
	} else {
		for (unsigned int i = first; i < last; i++) {
			struct mm_event_change *change = &changes->changes[i];
			struct mm_event_fd *sink = change->sink;

			// Reset the change flag.
			sink->changed = 0;
		}
	}
}

static void
mm_event_kqueue_commit_changes(struct mm_event_kqueue *backend,
			       struct mm_event_kqueue_storage *storage)
{
	ENTER();

	// Submit change events.
	int n = mm_kevent(backend->event_fd, storage->events, storage->nevents,
			  NULL, 0, NULL);
	DEBUG("kevent changed: %d, received: %d", storage->nevents, n);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "kevent");
		else
			mm_error(errno, "kevent");
	}
	storage->nevents = 0;

	LEAVE();
}

static int
mm_event_kqueue_poll(struct mm_event_kqueue *backend,
		     struct mm_event_kqueue_storage *storage,
		     mm_timeout_t timeout)
{
	ENTER();
	DEBUG("poll: changes: %d, timeout: %lu", storage->nevents, (unsigned long) timeout);

	struct timespec ts;
	if (timeout) {
		// Calculate the event wait timeout.
		ts.tv_sec = timeout / 1000000;
		ts.tv_nsec = (timeout % 1000000) * 1000;

		// Publish the log before a possible sleep.
		mm_log_relay();
	} else {
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
	}

	// Poll the system for events.
	int n = mm_kevent(backend->event_fd, storage->events, storage->nevents,
			  storage->events, MM_EVENT_KQUEUE_NEVENTS, &ts);
	DEBUG("kevent changed: %d, received: %d", storage->nevents, n);
	if (unlikely(n < 0)) {
		if (errno == EINTR)
			mm_warning(errno, "kevent");
		else
			mm_error(errno, "kevent");
		n = 0;
	}
	storage->nevents = 0;

	LEAVE();
	return n;
}

void NONNULL(1)
mm_event_kqueue_prepare(struct mm_event_kqueue *backend)
{
	ENTER();

	// Open a kqueue file descriptor.
	backend->event_fd = mm_kqueue();
	if (backend->event_fd == -1)
		mm_fatal(errno, "Failed to create kqueue");

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_cleanup(struct mm_event_kqueue *backend)
{
	ENTER();

	// Close the kqueue file descriptor.
	mm_close(backend->event_fd);

	LEAVE();
}

void NONNULL(1)
mm_event_kqueue_storage_prepare(struct mm_event_kqueue_storage *storage)
{
	ENTER();

	storage->nevents = 0;

	LEAVE();
}

void NONNULL(1, 2, 3)
mm_event_kqueue_listen(struct mm_event_kqueue *backend,
		       struct mm_event_kqueue_storage *storage,
		       struct mm_event_batch *changes,
		       struct mm_event_receiver *receiver,
		       mm_timeout_t timeout)
{
	ENTER();

	// Make event changes.
	unsigned int first = 0, next = 0;
	while (next < changes->nchanges) {
		struct mm_event_change *change = &changes->changes[next];
		if (likely(mm_event_kqueue_add_change(storage, change))) {
			// Proceed with more change events if any.
			next++;
		} else {
			// Flush event changes.
			mm_event_kqueue_commit_changes(backend, storage);

			// Store unregister events.
			mm_event_kqueue_postprocess_changes(changes, receiver, first, next);

			// Proceed with more change events if any.
			first = next;
			continue;
		}
	}

	if (receiver != NULL) {
		// Poll for incoming events.
		int n = mm_event_kqueue_poll(backend, storage, timeout);

		// Store incoming events.
		mm_event_kqueue_receive_events(storage, receiver, n);
	} else {
		// Flush event changes.
		mm_event_kqueue_commit_changes(backend, storage);
	}

	// Store unregister events.
	mm_event_kqueue_postprocess_changes(changes, receiver, first, changes->nchanges);

	LEAVE();
}

#if MM_EVENT_NATIVE_NOTIFY

bool NONNULL(1)
mm_event_kqueue_enable_notify(struct mm_event_kqueue *backend)
{
	ENTER();
	bool rc = true;

	static const struct kevent event = {
		.filter = EVFILT_USER,
		.ident = MM_EVENT_KQUEUE_NOTIFY_ID,
		.flags = EV_ADD | EV_CLEAR,
	};

	int n = mm_kevent(backend->event_fd, &event, 1, NULL, 0, NULL);
	DEBUG("kevent notify: %d", n);
	if (unlikely(n < 0)) {
		mm_warning(errno, "kevent");
		rc = false;
	}

	LEAVE();
	return rc;
}

void NONNULL(1)
mm_event_kqueue_notify(struct mm_event_kqueue *backend)
{
	ENTER();

	static const struct kevent event = {
		.filter = EVFILT_USER,
		.ident = MM_EVENT_KQUEUE_NOTIFY_ID,
		.fflags = NOTE_TRIGGER,
	};

	int n = mm_kevent(backend->event_fd, &event, 1, NULL, 0, NULL);
	DEBUG("kevent notify: %d", n);
	if (unlikely(n < 0))
		mm_error(errno, "kevent");

	LEAVE();
}

#endif

#endif /* HAVE_SYS_EVENT_H */
