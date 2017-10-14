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
#include "base/report.h"
#include "base/event/backend.h"
#include "base/event/epoch.h"
#include "base/thread/request.h"

/* Forward declarations. */
struct mm_domain;
struct mm_event_listener;
struct mm_ring_mpmc;
struct mm_strand;

/* Event dispatcher. */
struct mm_event_dispatch
{
	/* Event listeners. */
	struct mm_event_listener *listeners;
	mm_thread_t nlisteners;

	/* Shared request queue. */
	struct mm_ring_mpmc *request_queue;

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

/**********************************************************************
 * Shared requests for the dispatcher.
 **********************************************************************/

static inline void NONNULL(1)
mm_domain_notify(struct mm_event_dispatch *dispatch, mm_stamp_t stamp UNUSED)
{
	mm_event_wakeup_any(dispatch);
}

static inline bool NONNULL(1, 2)
mm_domain_receive(struct mm_event_dispatch *dispatch, struct mm_request_data *rdata)
{
	return mm_request_receive(dispatch->request_queue, rdata);
}

static inline void NONNULL(1, 2)
mm_domain_post_0(struct mm_event_dispatch *dispatch, mm_post_routine_t req)
{
	MM_POST(0, dispatch->request_queue, mm_domain_notify, dispatch, req);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_0(struct mm_event_dispatch *dispatch, mm_post_routine_t req)
{
	MM_TRYPOST(0, dispatch->request_queue, mm_domain_notify, dispatch, req);
}

static inline void NONNULL(1, 2)
mm_domain_post_1(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		 uintptr_t a1)
{
	MM_POST(1, dispatch->request_queue, mm_domain_notify, dispatch, req, a1);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_1(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		    uintptr_t a1)
{
	MM_TRYPOST(1, dispatch->request_queue, mm_domain_notify, dispatch, req, a1);
}

static inline void NONNULL(1, 2)
mm_domain_post_2(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2)
{
	MM_POST(2, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_2(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2)
{
	MM_TRYPOST(2, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2);
}

static inline void NONNULL(1, 2)
mm_domain_post_3(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_POST(3, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_3(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	MM_TRYPOST(3, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3);
}

static inline void NONNULL(1, 2)
mm_domain_post_4(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_POST(4, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3, a4);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_4(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	MM_TRYPOST(4, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3, a4);
}

static inline void NONNULL(1, 2)
mm_domain_post_5(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_POST(5, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3, a4, a5);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_5(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		    uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5)
{
	MM_TRYPOST(5, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3, a4, a5);
}

static inline void NONNULL(1, 2)
mm_domain_post_6(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		 uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_POST(6, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3, a4, a5, a6);
}

static inline bool NONNULL(1, 2)
mm_domain_trypost_6(struct mm_event_dispatch *dispatch, mm_post_routine_t req,
		   uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6)
{
	MM_TRYPOST(6, dispatch->request_queue, mm_domain_notify, dispatch, req, a1, a2, a3, a4, a5, a6);
}

#endif /* BASE_EVENT_DISPATCH_H */
