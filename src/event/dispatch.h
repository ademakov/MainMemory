/*
 * event/dispatch.h - MainMemory event dispatch.
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

#ifndef EVENT_DISPATCH_H
#define EVENT_DISPATCH_H

#include "common.h"
#include "base/list.h"
#include "core/lock.h"
#include "event/batch.h"
#include "event/epoll.h"
#include "event/event.h"
#include "event/kqueue.h"
#include "event/selfpipe.h"

#if HAVE_SYS_EPOLL_H
# include <sys/epoll.h>
#endif

#define MM_EVENT_NEVENTS	(512)

struct mm_dispatch
{
	mm_task_lock_t lock;

	/* The dispatch node chosen to do event poll. */
	struct mm_listener *polling_listener;

	/* The dispatch nodes that have nothing to do. */
	struct mm_list waiting_listeners;

	/* The event changes from waiting dispatch nodes. */
	struct mm_event_batch pending_changes;

	/* Event-loop self-pipe. */
	struct mm_selfpipe selfpipe;

#if HAVE_SYS_EPOLL_H
	struct mm_event_epoll events;
#endif
#if HAVE_SYS_EVENT_H
	struct mm_event_kqueue events;
#endif
};

void __attribute__((nonnull(1)))
mm_dispatch_prepare(struct mm_dispatch *dispatch);

void __attribute__((nonnull(1)))
mm_dispatch_cleanup(struct mm_dispatch *dispatch);

static inline void __attribute__((nonnull(1, 2, 3)))
mm_dispatch_listen(struct mm_dispatch *dispatch,
		   struct mm_event_batch *changes,
		   struct mm_event_batch *events,
		   mm_timeout_t timeout)
{
#if HAVE_SYS_EPOLL_H
	mm_event_epoll_listen(&dispatch->events, changes, events, timeout);
#endif
#if HAVE_SYS_EVENT_H
	mm_event_kqueue_listen(&dispatch->events, changes, events, timeout);
#endif
}

static inline void __attribute__((nonnull(1)))
mm_dispatch_notify(struct mm_dispatch *dispatch)
{
	mm_selfpipe_write(&dispatch->selfpipe);
}

static inline void __attribute__((nonnull(1)))
mm_dispatch_dampen(struct mm_dispatch *dispatch)
{
	mm_selfpipe_drain(&dispatch->selfpipe);
}

#endif /* EVENT_DISPATCH_H */