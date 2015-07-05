/*
 * event/listener.h - MainMemory event listener.
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

#ifndef EVENT_LISTENER_H
#define EVENT_LISTENER_H

#include "common.h"
#include "event/batch.h"
#include "event/event.h"

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

/* Forward declaration. */
struct mm_dispatch;
struct mm_event_backend;

typedef enum
{
	MM_LISTENER_RUNNING,
	MM_LISTENER_POLLING,
	MM_LISTENER_WAITING,

} mm_listener_state_t;

struct mm_listener
{
	/* Counters to pair listen/notify calls. */
	uint32_t listen_stamp;
	uint32_t notify_stamp;

	mm_listener_state_t state;

#if ENABLE_LINUX_FUTEX
	/* Nothing for futexes. */
#elif ENABLE_MACH_SEMAPHORE
	semaphore_t semaphore;
#else
	struct mm_monitor monitor;
#endif

	/* Auxiliary memory to store target listeners on dispatch. */
	struct mm_listener **dispatch_targets;

	/* Listener's private event change list. */
	struct mm_event_batch changes;
	/* Listener's private event list. */
	struct mm_event_batch events;
	/* Listener's finished events. */
	struct mm_event_batch finish;

} __mm_align_cacheline__;

void __attribute__((nonnull(1)))
mm_listener_prepare(struct mm_listener *listener,
		    struct mm_dispatch *dispatch);

void __attribute__((nonnull(1)))
mm_listener_cleanup(struct mm_listener *listener);

void __attribute__((nonnull(1, 2)))
mm_listener_notify(struct mm_listener *listener,
		   struct mm_event_backend *backend);

void __attribute__((nonnull(1)))
mm_listener_listen(struct mm_listener *listener,
		   struct mm_event_backend *backend,
		   mm_timeout_t timeout);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

static inline void __attribute__((nonnull(1, 2)))
mm_listener_register_fd(struct mm_listener *listener, struct mm_event_fd *ev_fd)
{
	mm_event_batch_add(&listener->changes, MM_EVENT_REGISTER, ev_fd);
	mm_event_batch_addflags(&listener->changes, MM_EVENT_BATCH_REGISTER);
}

static inline void __attribute__((nonnull(1, 2)))
mm_listener_unregister_fd(struct mm_listener *listener, struct mm_event_fd *ev_fd)
{
	mm_event_batch_add(&listener->changes, MM_EVENT_UNREGISTER, ev_fd);
	mm_event_batch_addflags(&listener->changes, MM_EVENT_BATCH_UNREGISTER);
}

static inline void __attribute__((nonnull(1, 2)))
mm_listener_trigger_input(struct mm_listener *listener, struct mm_event_fd *ev_fd)
{
#if MM_ONESHOT_HANDLERS
	mm_event_batch_add(&listener->changes, MM_EVENT_INPUT, ev_fd);
#else
	(void) listener;
	(void) ev_fd;
#endif
}

static inline void __attribute__((nonnull(1, 2)))
mm_listener_trigger_output(struct mm_listener *listener, struct mm_event_fd *ev_fd)
{
#if MM_ONESHOT_HANDLERS
	mm_event_batch_add(&listener->changes, MM_EVENT_OUTPUT, ev_fd);
#else
	(void) listener;
	(void) ev_fd;
#endif
}

static inline void __attribute__((nonnull(1, 2)))
mm_listener_dispatch_finish(struct mm_listener *listener, struct mm_event_fd *ev_fd)
{
	mm_event_batch_add(&listener->finish, MM_EVENT_DETACH, ev_fd);
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
		MM_EVENT_BATCH_REGISTER | MM_EVENT_BATCH_UNREGISTER);
}

#endif /* EVENT_LISTENER_H */
