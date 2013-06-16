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
#include "list.h"
#include "pool.h"
#include "runq.h"
#include "task.h"

/* Forward declaration. */
struct mm_timeq;

/* Virtual core state. */
struct mm_core
{
	/* Queue of ready to run tasks. */
	struct mm_runq run_queue;

	/* Queue of work items. */
	struct mm_list work_queue;
	/* Cache of free work items. */
	struct mm_list work_cache;

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
	/* The flag indicating that a master sleeps waiting for a work item. */
	bool master_waits_work;

	/* The master task. */
	struct mm_task *master;

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
	struct mm_list inbox __align(MM_CACHELINE);
	mm_core_lock_t inbox_lock;

	/* The memory chunks freed by other threads. */
	struct mm_list chunks __align(MM_CACHELINE);
	mm_global_lock_t chunks_lock;

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

void mm_core_update_time(void);
void mm_core_update_real_time(void);

#endif /* CORE_H */
