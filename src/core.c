/*
 * core.c - MainMemory core.
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

#include "core.h"

#include "alloc.h"
#include "clock.h"
#include "future.h"
#include "hook.h"
#include "port.h"
#include "sched.h"
#include "task.h"
#include "thread.h"
#include "timeq.h"
#include "timer.h"
#include "work.h"
#include "util.h"

#include "dlmalloc/malloc.h"

#include <stdio.h>

#if ENABLE_SMP
# define MM_DEFAULT_CORES	2
#else
# define MM_DEFAULT_CORES	1
#endif

#define MM_DEFAULT_WORKERS	512

#define MM_PRIO_MASTER		1
#define MM_PRIO_WORKER		MM_PRIO_DEFAULT

#define MM_TIME_QUEUE_MAX_WIDTH	500
#define MM_TIME_QUEUE_MAX_COUNT	2000

// The core set.
static int mm_core_num;
static struct mm_core *mm_core_set;

// A core associated with the running thread.
__thread struct mm_core *mm_core;

/**********************************************************************
 * Worker task.
 **********************************************************************/

static void
mm_core_worker_cleanup(uintptr_t arg __attribute__((unused)))
{
	mm_core->nworkers--;

	if (mm_core->master_waits_worker) {
		mm_sched_run(mm_core->master);
	}
}

static mm_result_t
mm_core_worker(uintptr_t arg)
{
	ENTER();

	struct mm_work *work = (struct mm_work *) arg;
	mm_routine_t routine = work->routine;
	uintptr_t routine_arg = work->item;
	mm_work_destroy(work);

	// Ensure cleanup on exit.
	mm_task_cleanup_push(mm_core_worker_cleanup, 0);

	// Execute the work routine.
	routine(routine_arg);

	// Cleanup on return.
	mm_task_cleanup_pop(true);

	LEAVE();
	return 0;
}

static void
mm_core_worker_start(struct mm_work *work)
{
	ENTER();

	struct mm_task *task = mm_task_create("worker", work->flags,
					      mm_core_worker, (uintptr_t) work);
	task->priority = MM_PRIO_WORKER;
	mm_core->nworkers++;
	mm_sched_run(task);

	LEAVE();
}

/**********************************************************************
 * Master task.
 **********************************************************************/

static mm_result_t
mm_core_master_loop(uintptr_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;

	while (!mm_memory_load(core->stop)) {

		if (core->nworkers == core->nworkers_max) {
			core->master_waits_worker = true;
			mm_sched_block();
			core->master_waits_worker = false;
		} else {
			struct mm_work *work = mm_work_get();
			if (likely(work != NULL)) {
				mm_core_worker_start(work);
			}
		}
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Core start and stop hooks.
 **********************************************************************/

static struct mm_hook mm_core_start_hook;
static struct mm_hook mm_core_param_start_hook;
static struct mm_hook mm_core_stop_hook;

static void
mm_core_free_hooks(void)
{
	ENTER();

	mm_hook_free(&mm_core_start_hook);
	mm_hook_free(&mm_core_stop_hook);

	LEAVE();
}

void
mm_core_hook_start(void (*proc)(void))
{
	ENTER();

	mm_hook_tail_proc(&mm_core_start_hook, proc);

	LEAVE();
}

void
mm_core_hook_param_start(void (*proc)(void *), void *data)
{
	ENTER();

	mm_hook_tail_data_proc(&mm_core_param_start_hook, proc, data);

	LEAVE();
}

void
mm_core_hook_stop(void (*proc)(void))
{
	ENTER();

	mm_hook_tail_proc(&mm_core_stop_hook, proc);

	LEAVE();
}

/**********************************************************************
 * Core initialization and termination.
 **********************************************************************/

/* A per-core thread entry point. */
static mm_result_t
mm_core_boot(uintptr_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;
	bool is_primary_core = (core == mm_core_set);

	// Set the thread-local pointer to the core object.
	mm_core = core;
	mm_core->thread = mm_thread;

	// Set the thread-local pointer to the running task.
	mm_running_task = mm_core->boot;
	mm_running_task->state = MM_TASK_RUNNING;

	// Update the time.
	mm_core_update_time();
	mm_core_update_real_time();

	// Create the time queue.
	core->time_queue = mm_timeq_create();
	mm_timeq_set_max_bucket_width(core->time_queue, MM_TIME_QUEUE_MAX_WIDTH);
	mm_timeq_set_max_bucket_count(core->time_queue, MM_TIME_QUEUE_MAX_COUNT);

	// Create the master task for this core and schedule it for execution.
	core->master = mm_task_create("master", 0, mm_core_master_loop, (uintptr_t) core);
	core->master->priority = MM_PRIO_MASTER;
	mm_sched_run(core->master);

	// Call the start hooks on the first core.
	if (is_primary_core) {
		mm_hook_call_proc(&mm_core_start_hook, false);
		mm_hook_call_data_proc(&mm_core_param_start_hook, false);
	}

	// Run the other tasks while there are any.
	mm_sched_block();

	// Call the stop hooks on the first core.
	if (is_primary_core)
		mm_hook_call_proc(&mm_core_stop_hook, false);

	// Destroy the time queue.
	mm_timeq_destroy(core->time_queue);

	// Invalidate the boot task.
	mm_running_task->state = MM_TASK_INVALID;

	LEAVE();
	return 0;
}

static void
mm_core_init_single(struct mm_core *core, uint32_t nworkers_max)
{
	ENTER();

	core->stop = 0;

	core->arena = create_mspace(0, 0);

	core->boot = mm_task_create_boot();

	core->thread = NULL;
	core->master = NULL;

	core->nworkers = 0;
	core->nworkers_max = nworkers_max;
	core->master_waits_worker = false;

	mm_runq_init(&core->run_queue);
	mm_list_init(&core->dead_list);

	core->time_value = 0;
	core->real_time_value = 0;
	core->time_queue = NULL;

	LEAVE();
}

static void
mm_core_term_single(struct mm_core *core)
{
	ENTER();

	mm_thread_destroy(core->thread);
	mm_task_destroy(core->boot);

	destroy_mspace(core->arena);

	LEAVE();
}

static void
mm_core_start_single(struct mm_core *core, int core_tag)
{
	ENTER();

	// Concoct a thread name.
	char name[MM_THREAD_NAME_SIZE];
	sprintf(name, "core %d", core_tag);

	// Set thread attributes.
	struct mm_thread_attr attr;
	mm_thread_attr_init(&attr);
	mm_thread_attr_setname(&attr, name);
	mm_thread_attr_setstack(&attr,
				core->boot->stack_base,
				core->boot->stack_size);
	mm_thread_attr_setcputag(&attr, core_tag);

	// Create a core thread.
	core->thread = mm_thread_create(&attr, &mm_core_boot, (uintptr_t) core);

	LEAVE();
}

static void
mm_core_stop_single(struct mm_core *core)
{
	ENTER();

	mm_memory_store(core->stop, 1);

	LEAVE();
}

void
mm_core_init(void)
{
	ENTER();
	ASSERT(mm_core_num == 0);

	mm_clock_init();
	mm_timer_init();
	mm_future_init();
	mm_thread_init();

	mm_work_init();
	mm_task_init();
	mm_port_init();

	// TODO: get the number of available CPU cores on the system.
	mm_core_num = MM_DEFAULT_CORES;

	mm_core_set = mm_alloc_aligned(MM_CACHELINE, mm_core_num * sizeof(struct mm_core));
	for (int i = 0; i < mm_core_num; i++)
		mm_core_init_single(&mm_core_set[i], MM_DEFAULT_WORKERS);

	LEAVE();
}

void
mm_core_term(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	for (int i = 0; i < mm_core_num; i++)
		mm_core_term_single(&mm_core_set[i]);
	mm_free(mm_core_set);

	mm_core_free_hooks();

	mm_task_term();
	mm_work_term();
	mm_port_term();
	
	mm_thread_init();
	mm_future_term();
	mm_timer_term();

	LEAVE();
}

void
mm_core_start(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	for (int i = 0; i < mm_core_num; i++)
		mm_core_start_single(&mm_core_set[i], i);
	for (int i = 0; i < mm_core_num; i++)
		mm_thread_join(mm_core_set[i].thread);

 	LEAVE();
}

void
mm_core_stop(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	for (int i = 0; i < mm_core_num; i++)
		mm_core_stop_single(&mm_core_set[i]);

	LEAVE();
}

/**********************************************************************
 * Core utilities.
 **********************************************************************/

void
mm_core_update_time(void)
{
	mm_core->time_value = mm_clock_gettime_monotonic();
	DEBUG("%lld", (long long) mm_core->time_value);
}

void
mm_core_update_real_time(void)
{
	mm_core->real_time_value = mm_clock_gettime_realtime();
	DEBUG("%lld", (long long) mm_core->real_time_value);
}
