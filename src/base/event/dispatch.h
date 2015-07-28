/*
 * base/event/dispatch.h - MainMemory event dispatch.
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

#ifndef BASE_EVENT_DISPATCH_H
#define BASE_EVENT_DISPATCH_H

#include "common.h"
#include "base/lock.h"
#include "base/event/backend.h"
#include "base/event/batch.h"
#include "base/event/event.h"
#include "base/event/receiver.h"
#include "base/log/debug.h"
#include "base/thread/thread.h"

struct mm_dispatch
{
	/* A lock that protects the change events. */
	mm_regular_lock_t lock;

	/* A thread elected to conduct the next event poll. */
	mm_thread_t control_thread;
#if ENABLE_DEBUG
	mm_thread_t last_control_thread;
#endif

	/* Counter for event busy waiting. */
	uint16_t busywait;

	/* A common store for published change events. */
	struct mm_event_batch changes;

	/* A common store for incoming events filled by the control thread. */
	struct mm_event_receiver receiver;

	/* A system-specific event backend. */
	struct mm_event_backend backend;
};

void __attribute__((nonnull(1, 3)))
mm_dispatch_prepare(struct mm_dispatch *dispatch,
		    mm_thread_t nthreads,
		    struct mm_thread *threads[]);

void __attribute__((nonnull(1)))
mm_dispatch_cleanup(struct mm_dispatch *dispatch);

static inline struct mm_listener * __attribute__((nonnull(1)))
mm_dispatch_listener(struct mm_dispatch *dispatch, mm_thread_t thread)
{
	return &dispatch->receiver.listeners[thread];
}

static inline void __attribute__((nonnull(1)))
mm_dispatch_notify(struct mm_dispatch *dispatch, mm_thread_t thread)
{
	ASSERT(thread < dispatch->nlisteners);
	struct mm_listener *listener = mm_dispatch_listener(dispatch, thread);
	mm_listener_notify(listener, &dispatch->backend);
}

void __attribute__((nonnull(1)))
mm_dispatch_notify_waiting(struct mm_dispatch *dispatch);

void __attribute__((nonnull(1)))
mm_dispatch_listen(struct mm_dispatch *dispatch, mm_thread_t thread,
		   mm_timeout_t timeout);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_register_fd(struct mm_dispatch *dispatch,
			struct mm_event_fd *sink)
{
	mm_thread_t thread = mm_event_target(sink);
	if (thread == MM_THREAD_NONE) {
		thread = sink->target = mm_thread_self();
	} else {
		ASSERT(mm_event_target(sink) == thread);
	}
	struct mm_listener *listener = mm_dispatch_listener(dispatch, thread);
	mm_listener_add(listener, sink, MM_EVENT_REGISTER);
	mm_listener_addflags(listener, MM_EVENT_BATCH_REGISTER);
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_unregister_fd(struct mm_dispatch *dispatch,
			  struct mm_event_fd *sink)
{
	mm_thread_t thread = mm_event_target(sink);
	ASSERT(thread == mm_thread_self());
	struct mm_listener *listener = mm_dispatch_listener(dispatch, thread);
	mm_listener_add(listener, sink, MM_EVENT_UNREGISTER);
	mm_listener_addflags(listener, MM_EVENT_BATCH_UNREGISTER);
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_trigger_input(struct mm_dispatch *dispatch,
			  struct mm_event_fd *sink)
{
#if MM_ONESHOT_HANDLERS
	mm_thread_t thread = mm_event_target(sink);
	ASSERT(thread == mm_thread_self());
	struct mm_listener *listener = mm_dispatch_listener(dispatch, thread);
	mm_listener_add(listener, sink, MM_EVENT_INPUT);
#else
	(void) dispatch;
	(void) sink;
#endif
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_trigger_output(struct mm_dispatch *dispatch,
			   struct mm_event_fd *sink)
{
#if MM_ONESHOT_HANDLERS
	mm_thread_t thread = mm_event_target(sink);
	ASSERT(thread == mm_thread_self());
	struct mm_listener *listener = mm_dispatch_listener(dispatch, thread);
	mm_listener_add(listener, sink, MM_EVENT_OUTPUT);
#else
	(void) dispatch;
	(void) sink;
#endif
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_detach(struct mm_dispatch *dispatch,
		   struct mm_event_fd *sink)
{
	mm_thread_t thread = mm_event_target(sink);
	ASSERT(thread == mm_thread_self());
	struct mm_listener *listener = mm_dispatch_listener(dispatch, thread);
	mm_listener_detach(listener, sink);
}

#endif /* BASE_EVENT_DISPATCH_H */
