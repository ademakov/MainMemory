/*
 * core.h - MainMemory core.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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
#include "wait.h"

/* Forward declarations. */
struct mm_bitset;
struct mm_chunk;
struct mm_event_table;
struct mm_timeq;
struct mm_net_server;

#define MM_CORE_SCHED_RING_SIZE		(1024)
#define MM_CORE_INBOX_RING_SIZE		(1024)
#define MM_CORE_CHUNK_RING_SIZE		(1024)

/* Virtual core state. */
struct mm_core
{
	/* Private memory arena. */
	void *arena;

	/* Queue of ready to run tasks. */
	struct mm_runq runq;

	/* Queue of tasks waiting for work items. */
	struct mm_list idle;

	/* The list of tasks that have finished. */
	struct mm_list dead;

	/* Queue of pending work items. */
	struct mm_queue workq;

	/* The number of items in the work queue. */
	uint32_t nwork;

	/* Current and maximum number of worker tasks. */
	uint32_t nidle;
	uint32_t nworkers;
	uint32_t nworkers_max;

	/* Cache of free wait entries. */
	struct mm_wait_cache wait_cache;

	/* Queue of delayed tasks. */
	struct mm_timeq *time_queue;

	/* The (almost) current time. */
	mm_timeval_t time_value;
	mm_timeval_t real_time_value;

	/* Master task. */
	struct mm_task *master;

	/* Dealer task. */
	struct mm_task *dealer;

	/* The bootstrap task. */
	struct mm_task *boot;

	/* The underlying thread. */
	struct mm_thread *thread;

	/* Memory pool for timers. */
	struct mm_pool timer_pool;
	/* Memory pool for futures. */
	struct mm_pool future_pool;

	/* Event poll data. */
	struct mm_event_table *events;
	mm_timeval_t poll_time;

	/*
	 * The fields below engage in cross-core communication.
	 */

	/* Stop flag. */
	bool stop;

	struct mm_synch *synch;

	/* Tasks to be scheduled. */
	MM_RING(sched, MM_CORE_SCHED_RING_SIZE);

	/* Submitted work items. */
	MM_RING(inbox, MM_CORE_INBOX_RING_SIZE);

	/* The memory chunks freed by other threads. */
	MM_RING(chunks, MM_CORE_CHUNK_RING_SIZE);

} __align(MM_CACHELINE);

void mm_core_init(void);
void mm_core_term(void);

void mm_core_hook_start(void (*proc)(void));
void mm_core_hook_param_start(void (*proc)(void *), void *data);
void mm_core_hook_stop(void (*proc)(void));
void mm_core_hook_param_stop(void (*proc)(void *), void *data);

void mm_core_register_server(struct mm_net_server *srv)
	__attribute__((nonnull(1)));

void mm_core_set_event_affinity(const struct mm_bitset *mask)
	__attribute__((nonnull(1)));

const struct mm_bitset * mm_core_get_event_affinity(void);

void mm_core_start(void);
void mm_core_stop(void);

void mm_core_post(mm_core_t core, mm_routine_t routine, mm_value_t routine_arg)
	__attribute__((nonnull(2)));

void mm_core_run_task(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_core_reclaim_chunk(struct mm_chunk *chunk);
void mm_core_reclaim_chain(struct mm_chunk *chunk);

/**********************************************************************
 * Core information.
 **********************************************************************/

extern mm_core_t mm_core_num;
extern struct mm_core *mm_core_set;

extern __thread struct mm_core *mm_core;

static inline mm_core_t
mm_core_getnum(void)
{
	return mm_core_num;
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
		return mm_core;
	ASSERT(core < mm_core_num);
	return &mm_core_set[core];
}

static inline mm_core_t
mm_core_self(void)
{
	return mm_core_getid(mm_core);
}

/**********************************************************************
 * Core time utilities.
 **********************************************************************/

static inline void
mm_core_update_time(struct mm_core *core)
{
	core->time_value = mm_clock_gettime_monotonic();
	TRACE("%lld", (long long) core->time_value);
}

static inline void
mm_core_update_real_time(struct mm_core *core)
{
	core->real_time_value = mm_clock_gettime_realtime();
	TRACE("%lld", (long long) core->real_time_value);
}

#endif /* CORE_H */
