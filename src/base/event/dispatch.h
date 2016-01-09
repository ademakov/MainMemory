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
#include "base/event/listener.h"
#include "base/event/receiver.h"
#include "base/log/debug.h"
#include "base/thread/thread.h"

struct mm_dispatch
{
	/* The thread domain associated with the dispatcher. */
	struct mm_domain *domain;

	/* Event listeners. */
	struct mm_event_listener *listeners;
	mm_thread_t nlisteners;

	/* A thread elected to conduct the next event poll. */
	mm_thread_t poller_thread;

	/* A lock that protects the poller thread election. */
	mm_regular_lock_t poller_lock;

	/* The event sink reclamation epoch. */
	uint32_t reclaim_epoch;

	/* A system-specific event backend. */
	struct mm_event_backend backend;
};

void NONNULL(1, 2, 4)
mm_dispatch_prepare(struct mm_dispatch *dispatch,
		    struct mm_domain *domain,
		    mm_thread_t nthreads,
		    struct mm_thread *threads[]);

void NONNULL(1)
mm_dispatch_cleanup(struct mm_dispatch *dispatch);

static inline struct mm_event_listener * NONNULL(1)
mm_dispatch_listener(struct mm_dispatch *dispatch, mm_thread_t thread)
{
	ASSERT(thread < dispatch->nlisteners);
	return &dispatch->listeners[thread];
}

static inline void NONNULL(1)
mm_dispatch_notify(struct mm_dispatch *dispatch, mm_thread_t thread)
{
	ASSERT(thread < dispatch->nlisteners);
	struct mm_event_listener *listener = mm_dispatch_listener(dispatch, thread);
	mm_event_listener_notify(listener);
}

void NONNULL(1)
mm_dispatch_listen(struct mm_dispatch *dispatch, mm_thread_t thread, mm_timeout_t timeout);

void NONNULL(1)
mm_dispatch_notify_waiting(struct mm_dispatch *dispatch);

/**********************************************************************
 * Reclamation epoch maintenance.
 **********************************************************************/

bool NONNULL(1)
mm_dispatch_advance_epoch(struct mm_dispatch *dispatch);

#endif /* BASE_EVENT_DISPATCH_H */
