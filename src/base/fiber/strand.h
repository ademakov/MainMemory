/*
 * base/fiber/strand.h - MainMemory fiber strand.
 *
 * Copyright (C) 2013-2017  Aleksey Demakov
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

#ifndef BASE_FIBER_STRAND_H
#define BASE_FIBER_STRAND_H

#include "common.h"

#include "base/list.h"
#include "base/fiber/runq.h"
#include "base/fiber/timer.h"
#include "base/fiber/wait.h"

/* Forward declarations. */
struct mm_event_dispatch;
struct mm_event_listener;
struct mm_fiber;
struct mm_work;

typedef enum
{
	MM_STRAND_INVALID = -1,
	MM_STRAND_RUNNING,
	MM_STRAND_CSWITCH,
} mm_strand_state_t;

/* A strand of fibers. */
struct mm_strand
{
	/* The counter of fiber context switches. */
	uint64_t cswitch_count;

	/* Currently running fiber. */
	struct mm_fiber *fiber;

	/* The strand status. */
	mm_strand_state_t state;

	/* Event dispatch support. */
	struct mm_event_listener *listener;
	struct mm_event_dispatch *dispatch;

	/* Queue of blocked fibers. */
	struct mm_list block;

	/* Queue of ready to run fibers. */
	struct mm_runq runq;

	/* Queue of fibers waiting for work items. */
	struct mm_list idle;

	/* List of fibers that have finished. */
	struct mm_list dead;

	/* List of asynchronous operations. */
	struct mm_list async;

	/* Queue of pending work items. */
	struct mm_queue workq;

	/* The number of items in the work queue. */
	uint32_t nwork;

	/* Current and maximum number of worker fibers. */
	mm_fiber_t nidle;
	mm_fiber_t nworkers;
	mm_fiber_t nworkers_min;
	mm_fiber_t nworkers_max;

	/* The counter of halting (and listening for events). */
	uint64_t halt_count;
	/* The counter of thread requests. */
	uint64_t thread_request_count;
	uint64_t domain_request_count;

	/* Cache of free wait entries. */
	struct mm_wait_cache wait_cache;

	/* Time-related data. */
	struct mm_time_manager time_manager;

	/* Master fiber. */
	struct mm_fiber *master;

	/* Dealer fiber. */
	struct mm_fiber *dealer;

	/* The bootstrap fiber. */
	struct mm_fiber *boot;

	/* The underlying thread. */
	struct mm_thread *thread;

	/*
	 * The fields below engage in cross-strand communication.
	 */

	/* Stop flag. */
	bool stop;

} CACHE_ALIGN;

/**********************************************************************
 * Strand initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_strand_prepare(struct mm_strand *strand);

void NONNULL(1)
mm_strand_cleanup(struct mm_strand *strand);

void NONNULL(1)
mm_strand_start(struct mm_strand *strand);

void NONNULL(1)
mm_strand_stop(struct mm_strand *strand);

/**********************************************************************
 * Strand fiber execution.
 **********************************************************************/

void NONNULL(1, 2)
mm_strand_add_work(struct mm_strand *strand, struct mm_work *work);

void NONNULL(1, 2)
mm_strand_submit_work(struct mm_strand *strand, struct mm_work *work);

void NONNULL(1)
mm_strand_tender_work(struct mm_work *work);

void NONNULL(1)
mm_strand_run_fiber(struct mm_fiber *fiber);

void NONNULL(1)
mm_strand_execute_requests(struct mm_strand *strand);

/**********************************************************************
 * Strand information.
 **********************************************************************/

extern __thread struct mm_strand *__mm_strand_self;

static inline struct mm_strand *
mm_strand_selfptr(void)
{
	return __mm_strand_self;
}

static inline mm_timeval_t
mm_strand_gettime(struct mm_strand *strand)
{
	return mm_timer_getclocktime(&strand->time_manager);
}

static inline mm_timeval_t
mm_strand_getrealtime(struct mm_strand *strand)
{
	return mm_timer_getrealclocktime(&strand->time_manager);
}

/**********************************************************************
 * Strand diagnostics and statistics.
 **********************************************************************/

void NONNULL(1)
mm_strand_print_fibers(struct mm_strand *strand);

void NONNULL(1)
mm_strand_stats(struct mm_strand *strand);

#endif /* BASE_FIBER_STRAND_H */
