/*
 * core/core.h - MainMemory core.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#ifndef CORE_CORE_H
#define CORE_CORE_H

#include "common.h"

#include "core/runq.h"
#include "core/timer.h"
#include "core/wait.h"

#include "base/list.h"
#include "base/memory/chunk.h"
#include "base/memory/memory.h"
#include "base/memory/pool.h"
#include "base/log/debug.h"
#include "base/ring.h"

/* Forward declarations. */
struct mm_bitset;
struct mm_chunk;
struct mm_net_server;
struct mm_task;
struct mm_work;

/* Virtual core state. */
struct mm_core
{
	/* Currently running task. */
	struct mm_task *task;

	/* Queue of ready to run tasks. */
	struct mm_runq runq;

	/* Queue of tasks waiting for work items. */
	struct mm_list idle;

	/* List of tasks that have finished. */
	struct mm_list dead;

	/* Queue of blocked tasks. */
	struct mm_list block;

	/* List of asynchronous operations. */
	struct mm_list async;

	/* Queue of pending work items. */
	struct mm_queue workq;

	/* The counter of task context switches. */
	uint64_t cswitch_count;
	/* The counter of thread requests. */
	uint64_t request_count;

	/* The number of items in the work queue. */
	uint32_t nwork;

	/* Current and maximum number of worker tasks. */
	uint32_t nidle;
	uint32_t nworkers;
	uint32_t nworkers_max;

	/* Cache of free wait entries. */
	struct mm_wait_cache wait_cache;

	/* Time-related data. */
	struct mm_time_manager time_manager;

	/* Master task. */
	struct mm_task *master;

	/* Dealer task. */
	struct mm_task *dealer;

	/* The bootstrap task. */
	struct mm_task *boot;

	/* The underlying thread. */
	struct mm_thread *thread;

	/*
	 * The fields below engage in cross-core communication.
	 */

	/* Stop flag. */
	bool stop;

} CACHE_ALIGN;

void mm_core_init(void);
void mm_core_term(void);

void mm_core_hook_start(void (*proc)(void));
void mm_core_hook_param_start(void (*proc)(void *), void *data);
void mm_core_hook_stop(void (*proc)(void));
void mm_core_hook_param_stop(void (*proc)(void *), void *data);

void NONNULL(1)
mm_core_register_server(struct mm_net_server *srv);

void NONNULL(1)
mm_core_set_event_affinity(const struct mm_bitset *mask);

const struct mm_bitset *
mm_core_get_event_affinity(void);

void mm_core_start(void);
void mm_core_stop(void);

void NONNULL(2)
mm_core_post(mm_core_t core, mm_routine_t routine, mm_value_t routine_arg);

void NONNULL(2)
mm_core_post_work(mm_core_t core_id, struct mm_work *work);

void NONNULL(1)
mm_core_run_task(struct mm_task *task);

void NONNULL(1)
mm_core_execute_requests(struct mm_core *core, uint32_t domain_limit);

/**********************************************************************
 * Core information.
 **********************************************************************/

extern mm_core_t mm_core_num;
extern struct mm_core *mm_core_set;

extern __thread struct mm_core *__mm_core_self;

/* Common event dispatch. */
extern struct mm_dispatch mm_core_dispatch;

static inline struct mm_core *
mm_core_selfptr(void)
{
	return __mm_core_self;
}

static inline mm_core_t
mm_core_getnum(void)
{
#if ENABLE_SMP
	return mm_core_num;
#else
	return 1;
#endif
}

static inline mm_core_t
mm_core_getid(struct mm_core *core)
{
	if (unlikely(core == NULL))
		return MM_CORE_NONE;
	return (mm_core_t) (core - mm_core_set);
}

static inline struct mm_core *
mm_core_getptr(mm_core_t core)
{
	if (core == MM_CORE_NONE)
		return NULL;
	if (core == MM_CORE_SELF)
		return mm_core_selfptr();
	ASSERT(core < mm_core_num);
	return &mm_core_set[core];
}

static inline mm_core_t
mm_core_self(void)
{
	return mm_core_getid(mm_core_selfptr());
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
mm_core_print_tasks(struct mm_core *core);

void
mm_core_stats(void);

#endif /* CORE_CORE_H */
