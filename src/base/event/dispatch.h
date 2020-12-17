/*
 * base/event/dispatch.h - MainMemory event dispatch.
 *
 * Copyright (C) 2015-2020  Aleksey Demakov
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
#include "base/event/epoch.h"

/* Event dispatch attributes. */
struct mm_event_dispatch_attr
{
	/* The number of event listeners. */
	mm_thread_t nlisteners;

	/* Individual listener parameters. */
	struct mm_event_dispatch_listener_attr *listeners_attr;
};

/* Event dispatcher. */
struct mm_event_dispatch
{
	/* Event listeners. */
	struct mm_event_listener *listeners;
	mm_thread_t nlisteners;

	/* A system-specific event backend. */
	struct mm_event_backend backend;

	/* The event sink reclamation epoch. */
	mm_event_epoch_t global_epoch;

#if ENABLE_SMP
	/* A lock that protects the poller thread election. */
	mm_regular_lock_t poll_lock CACHE_ALIGN;
#endif
};

/**********************************************************************
 * Event dispatcher setup and cleanup routines.
 **********************************************************************/

void NONNULL(1)
mm_event_dispatch_attr_prepare(struct mm_event_dispatch_attr *attr);

void NONNULL(1)
mm_event_dispatch_attr_cleanup(struct mm_event_dispatch_attr *attr);

void NONNULL(1)
mm_event_dispatch_attr_setlisteners(struct mm_event_dispatch_attr *attr, mm_thread_t n);

#if DISPATCH_ATTRS
void NONNULL(1, 3)
mm_event_dispatch_attr_setxxx(struct mm_event_dispatch_attr *attr, mm_thread_t n, xxx_t xxx);
#endif

void NONNULL(1, 2)
mm_event_dispatch_prepare(struct mm_event_dispatch *dispatch, const struct mm_event_dispatch_attr *attr);

void NONNULL(1)
mm_event_dispatch_cleanup(struct mm_event_dispatch *dispatch);

/**********************************************************************
 * Event statistics.
 **********************************************************************/

void NONNULL(1)
mm_event_dispatch_stats(struct mm_event_dispatch *dispatch);

#endif /* BASE_EVENT_DISPATCH_H */
