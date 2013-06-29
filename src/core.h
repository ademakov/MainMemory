/*
 * core.h - MainMemory core.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#ifndef CORE_H
#define CORE_H

#include "common.h"
#include "clock.h"
#include "list.h"
#include "pool.h"
#include "ring.h"
#include "runq.h"
#include "task.h"

/* Forward declaration. */
struct mm_timeq;

#define MM_CORE_INBOX_RING_SIZE		(1024)
#define MM_CORE_CHUNK_RING_SIZE		(1024)

/* Virtual core state. */
struct mm_core
{
	/* Queue of ready to run tasks. */
	struct mm_runq run_queue;

	/* Queue of work items. */
	struct mm_list work_queue;
	/* Cache of free work items. */
	struct mm_list work_cache;
	/* Queue of tasks waiting for work items. */
	struct mm_list wait_queue;

	/* Private memory arena. */
	void *arena;

	/* Queue of delayed tasks. */
	struct mm_timeq *time_queue;

	/* A current (almost) time. */
	mm_timeval_t time_value;
	mm_timeval_t real_time_value;

	/* Current and maximum number of worker tasks. */
	uint32_t nworkers;
	uint32_t nworkers_max;

	/* The list of worker tasks that have finished. */
	struct mm_list dead_list;

	/* Stop flag. */
	bool master_stop;

	/* Master task. */
	struct mm_task *master;

	/* Dealer task. */
	struct mm_task *dealer;

	/* The bootstrap task. */
	struct mm_task *boot;

	/* The underlying thread. */
	struct mm_thread *thread;

	/* The log message memory. */
	struct mm_list log_chunks;

	/* Memory pool for timers. */
	struct mm_pool timer_pool;
	/* Memory pool for futures. */
	struct mm_pool future_pool;

	/*
	 * The fields below engage in cross-core communication.
	 */

	/* Submitted work items. */
	struct mm_ring inbox;
	void *inbox_store[MM_CORE_INBOX_RING_SIZE];

	/* The memory chunks freed by other threads. */
	struct mm_ring chunk_ring;
	void *chunk_ring_store[MM_CORE_CHUNK_RING_SIZE];

} __align(MM_CACHELINE);

extern __thread struct mm_core *mm_core;

void mm_core_init(void);
void mm_core_term(void);

void mm_core_hook_start(void (*proc)(void));
void mm_core_hook_param_start(void (*proc)(void *), void *data);
void mm_core_hook_stop(void (*proc)(void));
void mm_core_hook_param_stop(void (*proc)(void *), void *data);

void mm_core_start(void);
void mm_core_stop(void);

void mm_core_add_work(mm_routine_t routine, uintptr_t routine_arg, bool pinned);

/**********************************************************************
 * Core time utilities.
 **********************************************************************/

static inline void
mm_core_update_time(void)
{
	mm_core->time_value = mm_clock_gettime_monotonic();
	DEBUG("%lld", (long long) mm_core->time_value);
}

static inline void
mm_core_update_real_time(void)
{
	mm_core->real_time_value = mm_clock_gettime_realtime();
	DEBUG("%lld", (long long) mm_core->real_time_value);
}

#endif /* CORE_H */
