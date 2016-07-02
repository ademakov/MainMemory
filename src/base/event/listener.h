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
#include "base/event/batch.h"
#include "base/event/receiver.h"

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
# include "base/thread/monitor.h"
#endif

/* Forward declarations. */
struct mm_event_dispatch;
struct mm_thread;

#define MM_EVENT_LISTENER_STATE		((uint32_t) 3)

typedef enum
{
	MM_EVENT_LISTENER_RUNNING = 0,
	MM_EVENT_LISTENER_POLLING = 1,
	MM_EVENT_LISTENER_WAITING = 2,
} mm_event_listener_state_t;

struct mm_event_listener
{
	/*
	 * The listener state.
	 *
	 * The two least-significant bits contain a mm_event_listener_state_t
	 * value. The rest of the bits contain the listen cycle counter.
	 */
	uint32_t listen_stamp;

	/*
	 * The listener notification state.
	 *
	 * The two least-significant bits always contain a zero value.
	 * The rest of the bits is matched against the listen_stamp
	 * to detect a pending notification.
	 */
	uint32_t notify_stamp;

#if ENABLE_LINUX_FUTEX
	/* Nothing for futexes. */
#elif ENABLE_MACH_SEMAPHORE
	semaphore_t semaphore;
#else
	struct mm_thread_monitor monitor;
#endif

	/* Associated thread. */
	struct mm_thread *thread;

	/* Listener's private change events store. */
	struct mm_event_batch changes;

	/* Listener's helper to receive events. */
	struct mm_event_receiver receiver;

	/* Counter for busy waiting. */
	uint16_t busywait;

} CACHE_ALIGN;

void NONNULL(1, 2, 3)
mm_event_listener_prepare(struct mm_event_listener *listener, struct mm_event_dispatch *dispatch,
			  struct mm_thread *thread);

void NONNULL(1)
mm_event_listener_cleanup(struct mm_event_listener *listener);

void NONNULL(1)
mm_event_listener_notify(struct mm_event_listener *listener);

void NONNULL(1)
mm_event_listener_poll(struct mm_event_listener *listener, mm_timeout_t timeout);

void NONNULL(1)
mm_event_listener_wait(struct mm_event_listener *listener, mm_timeout_t timeout);

static inline mm_event_listener_state_t NONNULL(1)
mm_event_listener_getstate(struct mm_event_listener *listener)
{
	uint32_t stamp = mm_memory_load(listener->listen_stamp);
	return (stamp & MM_EVENT_LISTENER_STATE);
}

/**********************************************************************
 * I/O events support.
 **********************************************************************/

static inline void NONNULL(1, 2)
mm_event_listener_add(struct mm_event_listener *listener, struct mm_event_fd *sink,
		      mm_event_change_t event)
{
	mm_event_batch_add(&listener->changes, event, sink);
}

static inline bool NONNULL(1)
mm_event_listener_has_changes(struct mm_event_listener *listener)
{
	return !mm_event_batch_empty(&listener->changes);
}

#endif /* BASE_EVENT_LISTENER_H */
