/*
 * base/event/kqueue.h - MainMemory kqueue support.
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

#ifndef BASE_EVENT_KQUEUE_H
#define BASE_EVENT_KQUEUE_H

#include "common.h"

#if HAVE_SYS_EVENT_H

#include <sys/types.h>
#include <sys/event.h>

#ifdef EVFILT_USER
# define MM_EVENT_NATIVE_NOTIFY		1
#endif

#define MM_EVENT_KQUEUE_NEVENTS		(64)

/* Forward declarations. */
struct mm_event_batch;
struct mm_event_change;
struct mm_event_listener;

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
	int nevents;
	/* The kevent list. */
	struct kevent events[MM_EVENT_KQUEUE_NEVENTS];

#if ENABLE_EVENT_STATS
	/* Statistics. */
	uint64_t nevents_stats[MM_EVENT_KQUEUE_NEVENTS + 1];
#endif
};

void NONNULL(1)
mm_event_kqueue_prepare(struct mm_event_kqueue *backend);

void NONNULL(1)
mm_event_kqueue_cleanup(struct mm_event_kqueue *backend);

void NONNULL(1)
mm_event_kqueue_storage_prepare(struct mm_event_kqueue_storage *storage);

void NONNULL(1, 2, 3)
mm_event_kqueue_listen(struct mm_event_kqueue *backend,
		       struct mm_event_batch *changes,
		       struct mm_event_listener *listener,
		       mm_timeout_t timeout);

void NONNULL(1, 2)
mm_event_kqueue_change(struct mm_event_kqueue *backend, struct mm_event_change *change);

#if MM_EVENT_NATIVE_NOTIFY

bool NONNULL(1)
mm_event_kqueue_enable_notify(struct mm_event_kqueue *backend);

void NONNULL(1)
mm_event_kqueue_notify(struct mm_event_kqueue *backend);

#endif

#endif /* HAVE_SYS_EVENT_H */
#endif /* BASE_EVENT_KQUEUE_H */
