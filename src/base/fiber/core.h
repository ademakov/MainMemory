/*
 * base/fiber/core.h - MainMemory core.
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

#ifndef BASE_FIBER_CORE_H
#define BASE_FIBER_CORE_H

#include "common.h"

#include "base/list.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/ring.h"
#include "base/fiber/runq.h"
#include "base/fiber/timer.h"
#include "base/fiber/wait.h"

/* Forward declarations. */
struct mm_fiber;
struct mm_work;

typedef enum
{
	MM_CORE_INVALID = -1,
	MM_CORE_RUNNING,
	MM_CORE_CSWITCH,
} mm_core_state_t;

/* Virtual core state. */
struct mm_core
{
	/* The counter of fiber context switches. */
	uint64_t cswitch_count;

	/* Currently running fiber. */
	struct mm_fiber *fiber;

	/* The core status. */
	mm_core_state_t state;

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
	 * The fields below engage in cross-core communication.
	 */

	/* Stop flag. */
	bool stop;

} CACHE_ALIGN;

/**********************************************************************
 * Core subsystem initialization and termination.
 **********************************************************************/

void mm_core_init(void);
void mm_core_term(void);

mm_value_t mm_core_boot(mm_value_t arg);
void mm_core_stop(void);

/**********************************************************************
 * Core task execution.
 **********************************************************************/

void NONNULL(2)
mm_core_post_work(mm_thread_t core_id, struct mm_work *work);

void NONNULL(1)
mm_core_run_fiber(struct mm_fiber *fiber);

void NONNULL(1)
mm_core_execute_requests(struct mm_core *core);

/**********************************************************************
 * Core information.
 **********************************************************************/

extern __thread struct mm_core *__mm_core_self;

static inline struct mm_core *
mm_core_selfptr(void)
{
	return __mm_core_self;
}

static inline mm_timeval_t
mm_core_gettime(struct mm_core *core)
{
	return mm_timer_getclocktime(&core->time_manager);
}

static inline mm_timeval_t
mm_core_getrealtime(struct mm_core *core)
{
	return mm_timer_getrealclocktime(&core->time_manager);
}

/**********************************************************************
 * Core diagnostics and statistics.
 **********************************************************************/

void NONNULL(1)
mm_core_print_fibers(struct mm_core *core);

void
mm_core_stats(void);

#endif /* BASE_FIBER_CORE_H */
