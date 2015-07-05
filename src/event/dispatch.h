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
#include "base/lock.h"
#include "event/backend.h"
#include "event/batch.h"

struct mm_dispatch
{
	/* The number of event listeners. */
	mm_thread_t nlisteners;

	mm_regular_lock_t lock;

	/* The listener elected to do event poll. */
	struct mm_listener *polling_listener;

	/* The listeners that have nothing to do. */
	struct mm_listener **waiting_listeners;

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

void __attribute__((nonnull(1, 2)))
mm_dispatch_checkin(struct mm_dispatch *dispatch, struct mm_listener *listener);

void __attribute__((nonnull(1, 2)))
mm_dispatch_checkout(struct mm_dispatch *dispatch, struct mm_listener *listener);

#endif /* EVENT_DISPATCH_H */
