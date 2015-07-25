/*
 * base/event/listener.h - MainMemory event listener.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#ifndef BASE_EVENT_LISTENER_H
#define BASE_EVENT_LISTENER_H

#include "common.h"
#include "base/lock.h"
#include "base/event/batch.h"
#include "base/event/event.h"

#if HAVE_LINUX_FUTEX_H
# define ENABLE_LINUX_FUTEX	1
#elif HAVE_MACH_SEMAPHORE_H
# define ENABLE_MACH_SEMAPHORE	1
#endif

#if ENABLE_LINUX_FUTEX
/* Nothing for futexes. */
#elif ENABLE_MACH_SEMAPHORE
# include <mach/semaphore.h>
#else
# include "base/thr/monitor.h"
#endif

/* Forward declarations. */
struct mm_event_backend;
struct mm_event_receiver;

typedef enum
{
	MM_LISTENER_RUNNING,
	MM_LISTENER_POLLING,
	MM_LISTENER_WAITING,

} mm_listener_state_t;

typedef enum
{
	MM_LISTENER_CHANGES_PRIVATE,
	MM_LISTENER_CHANGES_PUBLISHED,

} mm_listener_changes_state_t;

struct mm_listener
{
	/* The state of listening. */
	mm_listener_state_t state;

	/* Counters to pair listen/notify calls. */
	uint32_t listen_stamp;
	uint32_t notify_stamp;

	/* The state of pending changes. */
	mm_listener_changes_state_t changes_state;

	/* A counter to ensure visibility of change events. */
	uint32_t changes_stamp;

	/* A counter to detect detach feasibility. */
	uint32_t arrival_stamp;

	/* Listener's event sinks waiting to be detached. */
	struct mm_list detach_list;

#if ENABLE_LINUX_FUTEX
	/* Nothing for futexes. */
#elif ENABLE_MACH_SEMAPHORE
	semaphore_t semaphore;
#else
	struct mm_monitor monitor;
#endif

	/* Listener's private change events store. */
	struct mm_event_batch changes;
	/* Listener's incoming events store. */
	struct mm_event_batch events;

	/* A lock to protect the events store. */
	mm_regular_lock_t lock;

} __mm_align_cacheline__;

void __attribute__((nonnull(1)))
mm_listener_prepare(struct mm_listener *listener);

void __attribute__((nonnull(1)))
mm_listener_cleanup(struct mm_listener *listener);

void __attribute__((nonnull(1, 2)))
mm_listener_notify(struct mm_listener *listener,
		   struct mm_event_backend *backend);

void __attribute__((nonnull(1)))
mm_listener_listen(struct mm_listener *listener,
		   struct mm_event_backend *backend,
		   struct mm_event_receiver *receiver,
		   mm_timeout_t timeout);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

static inline void __attribute__((nonnull(1, 2)))
mm_listener_add(struct mm_listener *listener, struct mm_event_fd *ev_fd,
		mm_event_t event)
{
	mm_event_batch_add(&listener->changes, event, ev_fd);
}

static inline void __attribute__((nonnull(1)))
mm_listener_addflags(struct mm_listener *listener, unsigned int flags)
{
	mm_event_batch_addflags(&listener->changes, flags);
}

static inline void __attribute__((nonnull(1, 2)))
mm_listener_detach(struct mm_listener *listener, struct mm_event_fd *ev_fd)
{
	if (!ev_fd->pending_detach) {
		ev_fd->pending_detach = 1;
		mm_list_insert(&listener->detach_list,
				&ev_fd->detach_link);
	}
}

static inline bool __attribute__((nonnull(1)))
mm_listener_has_events(struct mm_listener *listener)
{
	return !mm_event_batch_empty(&listener->events);
}

static inline bool __attribute__((nonnull(1)))
mm_listener_has_changes(struct mm_listener *listener)
{
	return !mm_event_batch_empty(&listener->changes);
}

static inline bool __attribute__((nonnull(1)))
mm_listener_has_urgent_changes(struct mm_listener *listener)
{
	return mm_event_batch_hasflags(&listener->changes,
				       MM_EVENT_BATCH_UNREGISTER);
}

#endif /* BASE_EVENT_LISTENER_H */
