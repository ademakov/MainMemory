/*
 * base/event/dispatch.h - MainMemory event dispatch.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

/* Forward declarations. */
struct mm_strand;

/* Event dispatcher. */
struct mm_event_dispatch
{
	/* Event listeners. */
	struct mm_event_listener *listeners;
	mm_thread_t nlisteners;

	/* Asynchronous post queue. */
	struct mm_ring_mpmc *async_queue;

	/* A system-specific event backend. */
	struct mm_event_backend backend;

	/* The event sink reclamation epoch. */
	mm_event_epoch_t global_epoch;

	/* A lock that protects the poller thread election. */
	mm_regular_lock_t poller_lock CACHE_ALIGN;

	/* Counter for poller thread busy waiting. */
	uint16_t poller_spin;

	/* A coarse-grained lock that protects event sinks from
	   concurrent updates. */
	mm_regular_lock_t sink_lock CACHE_ALIGN;

	/* A queue of event sinks waiting for an owner thread. */
	struct mm_event_fd **sink_queue;
	uint16_t sink_queue_size;
	uint16_t sink_queue_head;
	uint16_t sink_queue_tail;
	uint16_t sink_queue_num;
};

void NONNULL(1, 3)
mm_event_dispatch_prepare(struct mm_event_dispatch *dispatch, mm_thread_t nthreads, struct mm_strand *strands,
			  uint32_t dispatch_queue_size, uint32_t listener_queue_size);

void NONNULL(1)
mm_event_dispatch_cleanup(struct mm_event_dispatch *dispatch);

/**********************************************************************
 * Event statistics.
 **********************************************************************/

void NONNULL(1)
mm_event_dispatch_stats(struct mm_event_dispatch *dispatch);

#endif /* BASE_EVENT_DISPATCH_H */
