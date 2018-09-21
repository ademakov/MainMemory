/*
 * base/event/kqueue.h - MainMemory kqueue support.
 *
 * Copyright (C) 2012-2018  Aleksey Demakov
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

#ifndef BASE_EVENT_KQUEUE_H
#define BASE_EVENT_KQUEUE_H

#include "common.h"

#if HAVE_SYS_EVENT_H

#include "base/event/event.h"

#include <sys/types.h>
#include <sys/event.h>

#ifdef EVFILT_USER
# define MM_EVENT_NATIVE_NOTIFY		1
#endif

#define MM_EVENT_KQUEUE_NEVENTS		(64)
#define MM_EVENT_KQUEUE_NCHANGES	(32)

/* Common data for kqueue support. */
struct mm_event_kqueue
{
	/* The kqueue file descriptor. */
	int event_fd;
};

/* Per-listener data for kqueue support. */
struct mm_event_kqueue_storage
{
	/* The kevent list size. */
	uint32_t nevents;
	/* The change list size. */
	uint32_t nchanges;
	/* The unregister list size. */
	uint32_t nunregister;

	/* The kevent change list. */
	struct kevent events[MM_EVENT_KQUEUE_NCHANGES];
	/* The kevent return list. */
	struct kevent revents[MM_EVENT_KQUEUE_NEVENTS];

	/* The changes list. */
	struct mm_event_fd *changes[MM_EVENT_KQUEUE_NCHANGES];
	/* The unregister list. */
	struct mm_event_fd *unregister[MM_EVENT_KQUEUE_NCHANGES];

#if ENABLE_EVENT_STATS
	/* Statistics. */
	uint64_t nevents_stats[MM_EVENT_KQUEUE_NEVENTS + 1];
#endif
};

/**********************************************************************
 * Event backend initialization and cleanup.
 **********************************************************************/

void NONNULL(1)
mm_event_kqueue_prepare(struct mm_event_kqueue *backend);

void NONNULL(1)
mm_event_kqueue_cleanup(struct mm_event_kqueue *backend);

void NONNULL(1)
mm_event_kqueue_storage_prepare(struct mm_event_kqueue_storage *storage);

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_kqueue_poll(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage,
		     mm_timeout_t timeout);

#if MM_EVENT_NATIVE_NOTIFY

bool NONNULL(1)
mm_event_kqueue_enable_notify(struct mm_event_kqueue *backend);

void NONNULL(1)
mm_event_kqueue_notify(struct mm_event_kqueue *backend);

#endif

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_kqueue_flush(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage);

static inline void NONNULL(1, 2, 3)
mm_event_kqueue_register_fd(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage,
			    struct mm_event_fd *sink)
{
	uint32_t input = sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER);
	uint32_t output = sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER);
	uint32_t n = (input != 0) + (output != 0);
	if (n) {
		if (unlikely((storage->nevents + n) > MM_EVENT_KQUEUE_NCHANGES))
			mm_event_kqueue_flush(backend, storage);

		if (input) {
			int flags = (sink->flags & MM_EVENT_REGULAR_INPUT) ? EV_ADD | EV_CLEAR : EV_ADD | EV_ONESHOT;
			struct kevent *kp = &storage->events[storage->nevents++];
			EV_SET(kp, sink->fd, EVFILT_READ, flags, 0, 0, sink);
		}
		if (output) {
			int flags = (sink->flags & MM_EVENT_REGULAR_OUTPUT) ? EV_ADD | EV_CLEAR : EV_ADD | EV_ONESHOT;
			struct kevent *kp = &storage->events[storage->nevents++];
			EV_SET(kp, sink->fd, EVFILT_WRITE, flags, 0, 0, sink);
		}
	}
}

static inline void NONNULL(1, 2, 3)
mm_event_kqueue_unregister_fd(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage,
			      struct mm_event_fd *sink)
{
	uint32_t input = sink->flags & (MM_EVENT_REGULAR_INPUT | MM_EVENT_INPUT_TRIGGER);
	uint32_t output = sink->flags & (MM_EVENT_REGULAR_OUTPUT | MM_EVENT_OUTPUT_TRIGGER);
	uint32_t n = (input != 0) + (output != 0);
	if (n) {
		if (unlikely((sink->flags & MM_EVENT_CHANGE) != 0)
		    || unlikely((storage->nevents + n) > MM_EVENT_KQUEUE_NCHANGES))
			mm_event_kqueue_flush(backend, storage);

		storage->unregister[storage->nunregister++] = sink;
		if (input) {
			struct kevent *kp = &storage->events[storage->nevents++];
			EV_SET(kp, sink->fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
		}
		if (output) {
			struct kevent *kp = &storage->events[storage->nevents++];
			EV_SET(kp, sink->fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
		}
	}
}

static inline void NONNULL(1, 2, 3)
mm_event_kqueue_trigger_input(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage,
			      struct mm_event_fd *sink)
{
	if (unlikely((sink->flags & MM_EVENT_CHANGE) != 0)
	    || unlikely(storage->nevents == MM_EVENT_KQUEUE_NCHANGES))
		mm_event_kqueue_flush(backend, storage);

	sink->flags |= MM_EVENT_CHANGE;
	storage->changes[storage->nchanges++] = sink;
	struct kevent *kp = &storage->events[storage->nevents++];
	EV_SET(kp, sink->fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, sink);
}

static inline void NONNULL(1, 2, 3)
mm_event_kqueue_trigger_output(struct mm_event_kqueue *backend, struct mm_event_kqueue_storage *storage,
			       struct mm_event_fd *sink)
{
	if (unlikely((sink->flags & MM_EVENT_CHANGE) != 0)
	    || unlikely(storage->nevents == MM_EVENT_KQUEUE_NCHANGES))
		mm_event_kqueue_flush(backend, storage);

	sink->flags |= MM_EVENT_CHANGE;
	storage->changes[storage->nchanges++] = sink;
	struct kevent *kp = &storage->events[storage->nevents++];
	EV_SET(kp, sink->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, sink);
}

#endif /* HAVE_SYS_EVENT_H */
#endif /* BASE_EVENT_KQUEUE_H */
