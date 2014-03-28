/*
 * core.c - MainMemory core.
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

#include "core.h"

#include "alloc.h"
#include "chunk.h"
#include "future.h"
#include "hook.h"
#include "log.h"
#include "net.h"
#include "port.h"
#include "task.h"
#include "thread.h"
#include "timeq.h"
#include "timer.h"
#include "trace.h"

#include "dlmalloc/malloc.h"

#include <stdio.h>
#include <unistd.h>

#define MM_DEFAULT_CORES	1
#define MM_DEFAULT_WORKERS	256

// Default dealer loop timeout - 1 second
#define MM_DEALER_TIMEOUT ((mm_timeout_t) 1000000)

#define MM_TIME_QUEUE_MAX_WIDTH	500
#define MM_TIME_QUEUE_MAX_COUNT	2000

// The core set.
mm_core_t mm_core_num;
struct mm_core *mm_core_set;

// A core associated with the running thread.
__thread struct mm_core *mm_core;

#define MM_CORE_IS_PRIMARY(core)	(core == mm_core_set)

/**********************************************************************
 * Core sleep and wakeup routines.
 **********************************************************************/

static void
mm_core_halt(struct mm_core *core, mm_timeout_t timeout)
{
	ENTER();

	if (MM_CORE_IS_PRIMARY(core)) {
		mm_event_dispatch(timeout);
	} else {
		mm_thread_timedwait(timeout);
	}

	LEAVE();
}

static void
mm_core_wake(struct mm_core *core)
{
	ENTER();

	if (MM_CORE_IS_PRIMARY(core)) {
		mm_event_notify();
	} else {
		mm_thread_signal(core->thread);
	}

	LEAVE();
}

/**********************************************************************
 * Idle queue.
 **********************************************************************/

void
mm_core_idle(struct mm_core *core)
{
	ENTER();

	struct mm_task *task = mm_running_task;
	ASSERT((task->flags & MM_TASK_CANCEL_ASYNCHRONOUS) == 0);

	// Put the task to the wait queue.
	ASSERT((task->flags & MM_TASK_WAITING) == 0);
	mm_list_insert(&core->idle, &task->wait_queue);
	task->flags |= MM_TASK_WAITING;
	core->nidle++;

	// Wait until poked.
	mm_task_block();

	LEAVE();
}

void
mm_core_idle_last(struct mm_core *core)
{
	ENTER();

	struct mm_task *task = mm_running_task;
	ASSERT((task->flags & MM_TASK_CANCEL_ASYNCHRONOUS) == 0);

	// Put the task to the wait queue.
	ASSERT((task->flags & MM_TASK_WAITING) == 0);
	mm_list_append(&core->idle, &task->wait_queue);
	task->flags |= MM_TASK_WAITING;
	core->nidle++;

	// Wait until poked.
	mm_task_block();

	LEAVE();
}

static void
mm_core_poke_idle(struct mm_core *core)
{
	ENTER();

	if (likely(!mm_list_empty(&core->idle))) {
		struct mm_list *link = mm_list_head(&core->idle);
		struct mm_task *task = containerof(link, struct mm_task, wait_queue);

		// Get a task from the wait queue.
		ASSERT((task->flags & MM_TASK_WAITING) != 0);
		mm_list_delete(&task->wait_queue);
		task->flags &= ~MM_TASK_WAITING;
		core->nidle--;

		// Put the task to the run queue.
		mm_task_run(task);
	}

	LEAVE();
}

/**********************************************************************
 * Work queue.
 **********************************************************************/

static void
mm_core_add_work(struct mm_core *core, struct mm_work *work)
{
	ENTER();

	// Enqueue the work item.
	mm_work_put(&core->workq, work);

	// If there is a task waiting for work then let it run now.
	mm_core_poke_idle(core);

	LEAVE();
}

void
mm_core_post(bool pinned, mm_routine_t routine, mm_value_t routine_arg)
{
	ENTER();

	// Create a work item.
	struct mm_work *work = mm_work_create(&mm_core->workq);
	mm_work_set(work, pinned, routine, routine_arg);

	// Enqueue it.
	mm_core_add_work(mm_core, work);

	LEAVE();
}

void
mm_core_submit(struct mm_core *core, mm_routine_t routine, mm_value_t routine_arg)
{
	ENTER();
	ASSERT(mm_core != NULL);

	// Create a work item.
	struct mm_work *work = mm_work_create(&mm_core->workq);
	mm_work_set(work, true, routine, routine_arg);

	if (core == mm_core) {
		// Enqueue it directly if on the same core.
		mm_core_add_work(core, work);
	} else {
		// Put the item to the target core inbox.
		for (;;) {
			bool ok = mm_ring_core_put(&core->inbox, work);

			// Wakeup the target core if it is asleep.
			mm_core_wake(core);

			if (ok) {
				break;
			} else {
				// TODO: backoff
				mm_task_yield();
			}
		}
	}

	LEAVE();
}

#if ENABLE_SMP
static bool
mm_core_receive_work(struct mm_core *core)
{
	struct mm_work *work = mm_ring_get(&core->inbox);
	if (work == NULL)
		return false;

	do {
		mm_core_add_work(core, work);
		work = mm_ring_get(&core->inbox);
	} while (work != NULL);

	return true;
}
#else
# define mm_core_receive_work(core) ({(void) core; false;})
#endif

/**********************************************************************
 * Task queue.
 **********************************************************************/

void
mm_core_run_task(struct mm_task *task)
{
	ENTER();

	if (task->core == mm_core) {
		// Put the task to the core run queue directly.
		mm_task_run(task);
	} else {
		// Put the task to the target core sched ring.
		for (;;) {
			bool ok = mm_ring_core_put(&task->core->sched, task);

			// Wakeup the target core if it is asleep.
			mm_core_wake(task->core);

			if (ok) {
				break;
			} else {
				// TODO: backoff
				mm_task_yield();
			}
		}
	}

	LEAVE();
}

#if ENABLE_SMP
static bool
mm_core_receive_tasks(struct mm_core *core)
{
	struct mm_task *task = mm_ring_get(&core->sched);
	if (task == NULL)
		return false;

	do {
		mm_task_run(task);
		task = mm_ring_get(&core->sched);
	} while (task != NULL);

	return true;
}
#else
# define mm_core_receive_tasks(core) ({(void) core; false;})
#endif

/**********************************************************************
 * Chunk reclamation.
 **********************************************************************/

void
mm_core_reclaim_chunk(struct mm_chunk *chunk)
{
	ENTER();

	if (chunk->core == mm_core) {
		// Destroy the chunk directly.
		mm_chunk_destroy(chunk);
	} else {
		// Put the chunk to the target core chunks ring.
		for (;;) {
			bool ok = mm_ring_global_put(&chunk->core->chunks, chunk);

			// Actual reclamation may wait a little bit so
			// don't wakeup the core unless the ring is full.
			if (ok) {
				break;
			} else if (unlikely(mm_memory_load(chunk->core->stop))) {
				mm_warning(0, "lost a chunk as core %d is stopped",
					   mm_core_getid(chunk->core));
				break;
			}

			// Wakeup the target core if it is asleep.
			mm_core_wake(chunk->core);
		}
	}

	LEAVE();
}

void
mm_core_reclaim_chain(struct mm_chunk *chunk)
{
	ENTER();

	while (chunk != NULL) {
		struct mm_chunk *next = chunk->next;
		mm_core_reclaim_chunk(chunk);
		chunk = next;
	}

	LEAVE();
}

static bool
mm_core_destroy_chunks(struct mm_core *core)
{
	struct mm_chunk *chunk = mm_ring_get(&core->chunks);
	if (chunk == NULL)
		return false;

	do {
		mm_chunk_destroy(chunk);
		chunk = mm_ring_get(&core->chunks);
	} while (chunk != NULL);

	return true;
}

/**********************************************************************
 * Worker task.
 **********************************************************************/

static void
mm_core_worker_cleanup(uintptr_t arg __attribute__((unused)))
{
	// Wake up the master possibly waiting for worker availability.
	if (mm_core->nworkers == mm_core->nworkers_max) {
		mm_task_run(mm_core->master);
	}

	// Account for the exiting worker.
	mm_core->nworkers--;
}

static mm_value_t
mm_core_worker(mm_value_t arg __attribute__((unused)))
{
	ENTER();

	// Ensure cleanup on exit.
	mm_task_cleanup_push(mm_core_worker_cleanup, 0);

	// Cache thread-specific data. This gives a smallish speedup for
	// the code emitted for the loop below on platforms with emulated
	// thread specific data (that is on Darwin).
	struct mm_core *core = mm_core;

	for (;;) {
		// Check to see if there is outstanding work.
		if (!mm_work_available(&core->workq)) {
			// Wait for work standing at the front of the idle queue.
			mm_core_idle(core);
			if (!mm_work_available(&core->workq)) {
				break;
			}
		}

		// Take the first available work item.
		struct mm_work *work = mm_work_get(&core->workq);

		// Save the work routine and recycle the work item.
		mm_routine_t routine = work->routine;
		mm_value_t routine_arg = work->routine_arg;
		mm_work_destroy(&core->workq, work);

		// Execute the work routine.
		routine(routine_arg);
	}

	// Cleanup on return.
	mm_task_cleanup_pop(true);

	LEAVE();
	return 0;
}

/**********************************************************************
 * Master task.
 **********************************************************************/

static mm_value_t
mm_core_master(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;

	while (!mm_memory_load(core->stop)) {

		// Check to see if there are enough workers.
		if (core->nworkers >= core->nworkers_max) {
			mm_task_block();
			continue;
		}

		// Check to see if there is outstanding work.
		if (mm_work_available(&core->workq)) {
			// Start a new worker to handle the work.
			struct mm_task_attr attr;
			mm_task_attr_init(&attr);
			mm_task_attr_setpriority(&attr, MM_PRIO_WORKER);
			mm_task_attr_setname(&attr, "worker");
			mm_task_create(&attr, mm_core_worker, 0);
			core->nworkers++;
		}

		// Wait for work at the back end of the idle queue.
		// So any idle worker would take work before the master.
		mm_core_idle_last(core);
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Dealer task.
 **********************************************************************/

static mm_value_t
mm_core_dealer(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;

	while (!mm_memory_load(core->stop)) {
		bool halt = true;
		if (mm_core_receive_work(core))
			halt = false;
		if (mm_core_receive_tasks(core))
			halt = false;

		if (halt) {
			mm_core_halt(core, MM_DEALER_TIMEOUT);
			mm_core_destroy_chunks(core);
		}

		mm_timer_tick();
		mm_task_yield();

		mm_wait_cache_truncate(&core->wait_cache);
		mm_core_destroy_chunks(core);
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
static struct mm_hook mm_core_param_stop_hook;

static void
mm_core_free_hooks(void)
{
	ENTER();

	mm_hook_free(&mm_core_start_hook);
	mm_hook_free(&mm_core_param_start_hook);
	mm_hook_free(&mm_core_stop_hook);
	mm_hook_free(&mm_core_param_stop_hook);

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

void
mm_core_hook_param_stop(void (*proc)(void *), void *data)
{
	ENTER();

	mm_hook_tail_data_proc(&mm_core_param_stop_hook, proc, data);

	LEAVE();
}

/**********************************************************************
 * Core initialization and termination.
 **********************************************************************/

static void
mm_core_boot_init(struct mm_core *core)
{
	mm_timer_init();
	mm_future_init();

	// Update the time.
	mm_core_update_time();
	mm_core_update_real_time();

	// Create the time queue.
	core->time_queue = mm_timeq_create();
	mm_timeq_set_max_bucket_width(core->time_queue, MM_TIME_QUEUE_MAX_WIDTH);
	mm_timeq_set_max_bucket_count(core->time_queue, MM_TIME_QUEUE_MAX_COUNT);

	// Create the master task for this core and schedule it for execution.
	struct mm_task_attr attr;
	mm_task_attr_init(&attr);
	mm_task_attr_setpriority(&attr, MM_PRIO_MASTER);
	mm_task_attr_setname(&attr, "master");
	core->master = mm_task_create(&attr, mm_core_master, (mm_value_t) core);

	// Create the dealer task for this core and schedule it for execution.
	mm_task_attr_setpriority(&attr, MM_PRIO_DEALER);
	mm_task_attr_setname(&attr, "dealer");
	core->dealer = mm_task_create(&attr, mm_core_dealer, (mm_value_t) core);

	// Call the start hooks on the first core.
	if (MM_CORE_IS_PRIMARY(core)) {
		mm_hook_call_proc(&mm_core_start_hook, false);
		mm_hook_call_data_proc(&mm_core_param_start_hook, false);
	}
}

static void
mm_core_boot_term(struct mm_core *core)
{
	// Call the stop hooks on the first core.
	if (MM_CORE_IS_PRIMARY(core)) {
		mm_hook_call_data_proc(&mm_core_param_stop_hook, false);
		mm_hook_call_proc(&mm_core_stop_hook, false);
	}

	mm_timeq_destroy(core->time_queue);

	mm_future_term();
	mm_timer_term();

	// TODO:
	//mm_task_destroy(core->master);
	//mm_task_destroy(core->dealer);
}

/* A per-core thread entry point. */
static mm_value_t
mm_core_boot(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;

	// Set the thread-local pointer to the core object.
	mm_core = core;
	mm_core->thread = mm_thread;

	// Set the thread-local pointer to the running task.
	mm_running_task = mm_core->boot;
	mm_running_task->state = MM_TASK_RUNNING;

	// Initialize per-core resources.
	mm_core_boot_init(core);

	// Run the other tasks while there are any.
	mm_task_yield();

	// Destroy per-core resources.
	mm_core_boot_term(core);

	// Invalidate the boot task.
	mm_running_task->state = MM_TASK_INVALID;
	mm_running_task = NULL;

	// Abandon the core.
	mm_core = NULL;

	LEAVE();
	return 0;
}

static void
mm_core_init_single(struct mm_core *core, uint32_t nworkers_max)
{
	ENTER();

	core->arena = create_mspace(0, 0);

	mm_runq_prepare(&core->runq);
	mm_list_init(&core->idle);
	mm_list_init(&core->dead);
	mm_work_prepare(&core->workq);
	mm_wait_cache_prepare(&core->wait_cache);

	core->stop = false;
	core->nidle = 0;
	core->nworkers = 0;
	core->nworkers_max = nworkers_max;

	core->time_queue = NULL;
	core->time_value = 0;
	core->real_time_value = 0;

	core->master = NULL;
	core->thread = NULL;

	core->log_head = NULL;
	core->log_tail = NULL;

	mm_ring_prepare(&core->sched, MM_CORE_SCHED_RING_SIZE);
	mm_ring_prepare(&core->inbox, MM_CORE_INBOX_RING_SIZE);
	mm_ring_prepare(&core->chunks, MM_CORE_CHUNK_RING_SIZE);

	// Create the core bootstrap task.
	struct mm_task_attr attr;
	mm_task_attr_init(&attr);
	mm_task_attr_setflags(&attr, MM_TASK_CANCEL_DISABLE);
	mm_task_attr_setpriority(&attr, MM_PRIO_BOOT);
	mm_task_attr_setstacksize(&attr, 0);
	mm_task_attr_setname(&attr, "boot");
	core->boot = mm_task_create(&attr, mm_core_boot, (mm_value_t) core);

	LEAVE();
}

static void
mm_core_term_inbox(struct mm_core *core)
{
	struct mm_work *work = mm_ring_get(&core->inbox);
	while (work != NULL) {
		mm_work_destroy(&core->workq, work);
		work = mm_ring_get(&core->inbox);
	}
}

static void
mm_core_term_single(struct mm_core *core)
{
	ENTER();

	mm_core_term_inbox(core);

	mm_work_cleanup(&core->workq);
	mm_wait_cache_cleanup(&core->wait_cache);

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
	core->thread = mm_thread_create(&attr,
					core->boot->start,
					core->boot->start_arg);

	LEAVE();
}

static int
mm_core_get_num(void)
{
#if ENABLE_SMP
# if defined(_SC_NPROCESSORS_ONLN)
	int nproc_onln = sysconf(_SC_NPROCESSORS_ONLN);
	if (nproc_onln < 0)
		mm_fatal(errno, "Failed to count cores.");
	return nproc_onln;
# else
#  error "Unsupported SMP architecture."
# endif
#endif
	return MM_DEFAULT_CORES;
}

void
mm_core_init(void)
{
	ENTER();
	ASSERT(mm_core_num == 0);

	dlmallopt(M_GRANULARITY, 16 * MM_PAGE_SIZE);

	mm_core_num = mm_core_get_num();
	ASSERT(mm_core_num > 0);
	if (mm_core_num == 1)
		mm_brief("Running on 1 core.");
	else
		mm_brief("Running on %d cores.", mm_core_num);

	mm_clock_init();
	mm_thread_init();

	mm_task_init();
	mm_port_init();
	mm_wait_init();
	mm_work_init();

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
	mm_port_term();
	mm_wait_term();
	mm_work_term();

	mm_thread_term();

	LEAVE();
}

void
mm_core_register_server(struct mm_net_server *srv)
{
	ENTER();

	// Register the server start hook.
	mm_core_hook_param_start((mm_hook_rtn1) mm_net_start_server, srv);

	// Register the server stop hook.
	mm_core_hook_param_stop((mm_hook_rtn1) mm_net_stop_server, srv);

	LEAVE();
}

void
mm_core_start(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	// Start core threads.
	for (int i = 0; i < mm_core_num; i++)
		mm_core_start_single(&mm_core_set[i], i);

	// Loop until stopped.
	while (!mm_exit_test()) {
		size_t logged = mm_log_write();
		usleep(logged ? 10000 : 1000000);
		DEBUG("cycle");
	}

	// Wait for core threads completion.
	for (int i = 0; i < mm_core_num; i++)
		mm_thread_join(mm_core_set[i].thread);

 	LEAVE();
}

void
mm_core_stop(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	// Set stop flag for core threads.
	for (int i = 0; i < mm_core_num; i++)
		mm_memory_store(mm_core_set[i].stop, true);

	LEAVE();
}
