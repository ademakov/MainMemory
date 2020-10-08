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

#include <sys/epoll.h>

/* Forward declarations. */
struct mm_event_fd;

#define MM_EVENT_EPOLL_NEVENTS		(64)

/* Common data for epoll support. */
struct mm_event_epoll
{
	/* The epoll file descriptor. */
	int event_fd;
};

/* A deferred event (used for errors). */
struct mm_event_epoll_stash
{
	struct mm_event_fd *sink;
	uint32_t flags;
};

/* Per-listener data for epoll support. */
struct mm_event_epoll_local
{
	/* A private epoll instance. */
	struct mm_event_epoll poll;

	/* The eventfd descriptor used for notification. */
	int notify_fd;

	/* Notification flag. */
	bool notified;

	/* Deferred events (used for errors). */
	uint32_t stash_size;
	uint32_t stash_capacity;
	struct mm_event_epoll_stash *stash;

	/* The epoll list. */
	struct epoll_event events[MM_EVENT_EPOLL_NEVENTS];

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

void NONNULL(1, 2)
mm_event_epoll_local_prepare(struct mm_event_epoll_local *local, struct mm_event_epoll *common);

void NONNULL(1)
mm_event_epoll_local_cleanup(struct mm_event_epoll_local *local);

/**********************************************************************
 * Event backend poll and notify routines.
 **********************************************************************/

void NONNULL(1, 2)
mm_event_epoll_poll(struct mm_event_epoll_local *local, struct mm_event_epoll *common, mm_timeout_t timeout);

void NONNULL(1)
mm_event_epoll_notify(struct mm_event_epoll_local *local);

void NONNULL(1)
mm_event_epoll_notify_clean(struct mm_event_epoll_local *local);

/**********************************************************************
 * Event sink I/O control.
 **********************************************************************/

void NONNULL(1, 2, 3)
mm_event_epoll_register_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink);

void NONNULL(1, 2, 3)
mm_event_epoll_unregister_fd(struct mm_event_epoll_local *local, struct mm_event_epoll *common, struct mm_event_fd *sink);

void NONNULL(1, 2)
mm_event_epoll_trigger_input(struct mm_event_epoll_local *local, struct mm_event_fd *sink);

void NONNULL(1, 2)
mm_event_epoll_trigger_output(struct mm_event_epoll_local *local, struct mm_event_fd *sink);

#endif /* HAVE_SYS_EPOLL_H */
#endif /* BASE_EVENT_EPOLL_H */
