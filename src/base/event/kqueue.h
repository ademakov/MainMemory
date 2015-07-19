/*
 * base/event/kqueue.h - MainMemory kqueue support.
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

#ifndef BASE_EVENT_KQUEUE_H
#define BASE_EVENT_KQUEUE_H

#include "common.h"

#if HAVE_SYS_EVENT_H

#include <sys/event.h>

#define MM_EVENT_KQUEUE_NEVENTS	(512)

/* Forward declarations. */
struct mm_event_batch;
struct mm_event_receiver;

/* Data for kqueue support. */
struct mm_event_kqueue
{
	/* The kqueue file descriptor. */
	int event_fd;

	/* The kevent list size. */
	int nevents;

	/* The kevent list. */
	struct kevent events[MM_EVENT_KQUEUE_NEVENTS];
};

void __attribute__((nonnull(1)))
mm_event_kqueue_prepare(struct mm_event_kqueue *event_backend);

void __attribute__((nonnull(1)))
mm_event_kqueue_cleanup(struct mm_event_kqueue *event_backend);

void __attribute__((nonnull(1, 2, 3)))
mm_event_kqueue_listen(struct mm_event_kqueue *event_backend,
		       struct mm_event_batch *change_events,
		       struct mm_event_receiver *return_events,
		       mm_timeout_t timeout);

#endif /* HAVE_SYS_EVENT_H */
#endif /* BASE_EVENT_KQUEUE_H */
