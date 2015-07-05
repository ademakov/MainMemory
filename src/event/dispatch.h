/*
 * event/dispatch.h - MainMemory event dispatch.
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

#ifndef EVENT_DISPATCH_H
#define EVENT_DISPATCH_H

#include "common.h"
#include "base/bitset.h"
#include "base/lock.h"
#include "base/log/debug.h"
#include "base/thread/thread.h"
#include "event/backend.h"
#include "event/batch.h"
#include "event/event.h"
#include "event/listener.h"

struct mm_dispatch
{
	mm_regular_lock_t lock;

	/* Event listeners. */
	struct mm_listener *listeners;
	mm_thread_t nlisteners;

	/* The listener elected to do event poll. */
	mm_thread_t polling_listener;

	/* The listeners that have nothing to do. */
	struct mm_bitset waiting_listeners;

	/* The event changes from waiting listeners. */
	struct mm_event_batch pending_changes;

	/* The events to be delivered to listeners. */
	struct mm_event_batch *pending_events;

	/* The system backend. */
	struct mm_event_backend backend;
};

void __attribute__((nonnull(1)))
mm_dispatch_prepare(struct mm_dispatch *dispatch, mm_thread_t nlisteners);

void __attribute__((nonnull(1)))
mm_dispatch_cleanup(struct mm_dispatch *dispatch);

static inline void __attribute__((nonnull(1)))
mm_dispatch_notify(struct mm_dispatch *dispatch, mm_thread_t tid)
{
	ASSERT(tid < dispatch->nlisteners);
	struct mm_listener *listener = &dispatch->listeners[tid];
	mm_listener_notify(listener, &dispatch->backend);
}

void __attribute__((nonnull(1)))
mm_dispatch_listen(struct mm_dispatch *dispatch, mm_thread_t tid,
		   mm_timeout_t timeout);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_register_fd(struct mm_dispatch *dispatch,
			struct mm_event_fd *ev_fd)
{
	ASSERT(mm_event_target(ev_fd) == MM_THREAD_NONE);
	mm_thread_t tid = mm_thread_getnumber(mm_thread_self());
	struct mm_listener *listener = &dispatch->listeners[tid];
	mm_listener_add(listener, ev_fd, MM_EVENT_REGISTER);
	mm_listener_addflags(listener, MM_EVENT_BATCH_REGISTER);
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_unregister_fd(struct mm_dispatch *dispatch,
			  struct mm_event_fd *ev_fd)
{
	mm_thread_t tid = mm_event_target(ev_fd);
	ASSERT(tid == mm_thread_getnumber(mm_thread_self()));
	struct mm_listener *listener = &dispatch->listeners[tid];
	mm_listener_add(listener, ev_fd, MM_EVENT_UNREGISTER);
	mm_listener_addflags(listener, MM_EVENT_BATCH_UNREGISTER);
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_trigger_input(struct mm_dispatch *dispatch,
			  struct mm_event_fd *ev_fd)
{
#if MM_ONESHOT_HANDLERS
	mm_thread_t tid = mm_event_target(ev_fd);
	ASSERT(tid == mm_thread_getnumber(mm_thread_self()));
	struct mm_listener *listener = &dispatch->listeners[tid];
	mm_listener_add(listener, ev_fd, MM_EVENT_INPUT);
#else
	(void) dispatch;
	(void) ev_fd;
#endif
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_trigger_output(struct mm_dispatch *dispatch,
			   struct mm_event_fd *ev_fd)
{
#if MM_ONESHOT_HANDLERS
	mm_thread_t tid = mm_event_target(ev_fd);
	ASSERT(tid == mm_thread_getnumber(mm_thread_self()));
	struct mm_listener *listener = &dispatch->listeners[tid];
	mm_listener_add(listener, ev_fd, MM_EVENT_OUTPUT);
#else
	(void) dispatch;
	(void) ev_fd;
#endif
}

static inline void __attribute__((nonnull(1, 2)))
mm_dispatch_detach(struct mm_dispatch *dispatch,
		   struct mm_event_fd *ev_fd)
{
	mm_thread_t tid = mm_event_target(ev_fd);
	ASSERT(tid == mm_thread_getnumber(mm_thread_self()));
	struct mm_listener *listener = &dispatch->listeners[tid];
	mm_listener_detach(listener, ev_fd);
}

#endif /* EVENT_DISPATCH_H */
