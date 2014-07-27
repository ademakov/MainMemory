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
#include "bitset.h"
#include "cdata.h"
#include "chunk.h"
#include "future.h"
#include "hook.h"
#include "log.h"
#include "net.h"
#include "port.h"
#include "synch.h"
#include "task.h"
#include "thread.h"
#include "timeq.h"
#include "timer.h"
#include "trace.h"

#include "dlmalloc/malloc.h"

#include <stdio.h>
#include <unistd.h>

#ifdef HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif

#define MM_DEFAULT_CORES	1
#define MM_DEFAULT_WORKERS	256

#define MM_TIME_QUEUE_MAX_WIDTH	500
#define MM_TIME_QUEUE_MAX_COUNT	2000

#if ENABLE_SMP
# define MM_CORE_IS_PRIMARY(core)	(core == mm_core_set)
#else
# define MM_CORE_IS_PRIMARY(core)	(true)
#endif

/* A work item. */
struct mm_work
{
	/* A link in the work queue. */
	struct mm_link link;

	/* The work is pinned to a specific core. */
	bool pinned;

	/* The work routine. */
	mm_routine_t routine;
	/* The work routine argument. */
	mm_value_t routine_arg;
};

// The core set.
mm_core_t mm_core_num;
struct mm_core *mm_core_set;

// A core associated with the running thread.
__thread struct mm_core *mm_core;

// The set of cores with event loops.
static struct mm_bitset mm_core_event_affinity;

/**********************************************************************
 * Idle queue.
 **********************************************************************/

void
mm_core_idle(struct mm_core *core, bool tail)
{
	ENTER();

	struct mm_task *task = mm_task_self();
	ASSERT((task->flags & MM_TASK_CANCEL_ASYNCHRONOUS) == 0);

	// Put the task into the wait queue.
	if (tail)
		mm_list_append(&core->idle, &task->wait_queue);
	else
		mm_list_insert(&core->idle, &task->wait_queue);

	ASSERT((task->flags & MM_TASK_WAITING) == 0);
	task->flags |= MM_TASK_WAITING;
	core->nidle++;

	// Wait until poked.
	mm_task_block();

	// Normally an idle task starts after being poked and
	// in this case it should already be removed from the
	// wait list. But if the task has started for another
	// reason it must be removed from the wait list here.
	if (unlikely((task->flags & MM_TASK_WAITING) != 0)) {
		mm_list_delete(&task->wait_queue);
		task->flags &= ~MM_TASK_WAITING;
		core->nidle--;
	}

	LEAVE();
}

static void
mm_core_poke(struct mm_core *core)
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
 * Work pool.
 **********************************************************************/

// The memory pool for work items.
static struct mm_pool mm_core_work_pool;

static void
mm_core_start_work(void)
{
	ENTER();

	mm_pool_prepare_shared(&mm_core_work_pool, "work", sizeof(struct mm_work));

	LEAVE();
}

static void
mm_core_stop_work(void)
{
	ENTER();

	mm_pool_cleanup(&mm_core_work_pool);

	LEAVE();
}

static void
mm_core_init_work(void)
{
	ENTER();

	mm_core_hook_start(mm_core_start_work);
	mm_core_hook_stop(mm_core_stop_work);

	LEAVE();
}

static inline struct mm_work *
mm_core_create_work(struct mm_core *core, bool pinned, mm_routine_t routine, mm_value_t routine_arg)
{
	struct mm_work *work = mm_pool_shared_alloc_low(mm_core_getid(core), &mm_core_work_pool);
	work->pinned = pinned;
	work->routine = routine;
	work->routine_arg = routine_arg;
	return work;
}

static inline void
mm_core_destroy_work(struct mm_core *core, struct mm_work *work)
{
	mm_pool_shared_free_low(mm_core_getid(core), &mm_core_work_pool, work);
}

/**********************************************************************
 * Work queue.
 **********************************************************************/

static inline bool
mm_core_has_work(struct mm_core *core)
{
	return core->nwork != 0;
}

static struct mm_work *
mm_core_get_work(struct mm_core *core)
{
	ASSERT(mm_core_has_work(core));

	core->nwork--;
	struct mm_link *link = mm_queue_delete_head(&core->workq);
	return containerof(link, struct mm_work, link);
}

static void
mm_core_add_work(struct mm_core *core, struct mm_work *work)
{
	ENTER();

	// Enqueue the work item.
	mm_queue_append(&core->workq, &work->link);
	core->nwork++;

	// If there is a task waiting for work then let it run now.
	mm_core_poke(core);

	LEAVE();
}

void
mm_core_post(mm_core_t core_id, mm_routine_t routine, mm_value_t routine_arg)
{
	ENTER();

	// Get the destination core.
	bool pinned;
	struct mm_core *core;
	if (core_id != MM_CORE_NONE) {
		pinned = true;
		core = mm_core_getptr(core_id);
	} else {
		pinned = false;
		core = mm_core;
	}

	// Create a work item.
	struct mm_work *work = mm_core_create_work(mm_core, pinned, routine, routine_arg);
	if (core == mm_core) {
		// Enqueue it directly if on the same core.
		mm_core_add_work(core, work);
	} else {
		// Put the item to the target core inbox.
		for (;;) {
			bool ok = mm_ring_shared_put(&core->inbox, work);

			// Wakeup the target core if it is asleep.
			mm_synch_signal(core->synch);

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
static void
mm_core_receive_work(struct mm_core *core)
{
	ENTER();

	struct mm_work *work = mm_ring_get(&core->inbox);
	while (work != NULL) {
		mm_core_add_work(core, work);
		work = mm_ring_get(&core->inbox);
	} 

	LEAVE();
}
#else
# define mm_core_receive_work(core) ((void) core)
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
			bool ok = mm_ring_shared_put(&task->core->sched, task);

			// Wakeup the target core if it is asleep.
			mm_synch_signal(task->core->synch);

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
static void
mm_core_receive_tasks(struct mm_core *core)
{
	ENTER();

	struct mm_task *task = mm_ring_get(&core->sched);
	while (task != NULL) {
		mm_task_run(task);
		task = mm_ring_get(&core->sched);
	}

	LEAVE();
}
#else
# define mm_core_receive_tasks(core) ((void) core)
#endif

/**********************************************************************
 * Chunk reclamation.
 **********************************************************************/

void
mm_core_reclaim_chunk(struct mm_chunk *chunk)
{
	ENTER();

	if (chunk->core == mm_core_selfid()) {
		// Destroy the chunk directly.
		mm_chunk_destroy(chunk);
	} else {
		// Put the chunk to the target core chunks ring.
		struct mm_core *core = mm_core_getptr(chunk->core);
		for (;;) {
			bool ok = mm_ring_global_put(&core->chunks, chunk);

			// Actual reclamation may wait a little bit so
			// don't wakeup the core unless the ring is full.
			if (ok) {
				break;
			} else if (unlikely(mm_memory_load(core->stop))) {
				mm_warning(0, "lost a chunk as core %d is stopped",
					   chunk->core);
				break;
			}

			// Wakeup the target core if it is asleep.
			mm_synch_signal(core->synch);
		}
	}

	LEAVE();
}

void
mm_core_reclaim_chain(struct mm_chunk *chunk)
{
	ENTER();

	if (chunk != NULL) {
		for (;;) {
			struct mm_link *link = chunk->link.next;
			mm_core_reclaim_chunk(chunk);
			if (link == NULL)
				break;
			chunk = containerof(link, struct mm_chunk, link);
		}
	}

	LEAVE();
}

static void
mm_core_destroy_chunks(struct mm_core *core)
{
	ENTER();

	struct mm_chunk *chunk = mm_ring_get(&core->chunks);
	while (chunk != NULL) {
		mm_chunk_destroy(chunk);
		chunk = mm_ring_get(&core->chunks);
	}

	LEAVE();
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
mm_core_worker(mm_value_t arg)
{
	ENTER();

	// Ensure cleanup on exit.
	mm_task_cleanup_push(mm_core_worker_cleanup, 0);

	// Cache thread-specific data. This gives a smallish speedup for
	// the code emitted for the loop below on platforms with emulated
	// thread specific data (that is on Darwin).
	struct mm_core *core = mm_core;

	// Take the work item supplied by the master.
	struct mm_work *work = (struct mm_work *) arg;

	for (;;) {
		// Save the work data and recycle the work item.
		mm_routine_t routine = work->routine;
		mm_value_t routine_arg = work->routine_arg;
		mm_core_destroy_work(core, work);

		// Execute the work routine.
		routine(routine_arg);

		// Check to see if there is outstanding work.
		while (!mm_core_has_work(core)) {
			// Wait for work standing at the front of the idle queue.
			mm_core_idle(core, false);
		}

		// Take the first available work item.
		work = mm_core_get_work(core);
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
		if (mm_core_has_work(core)) {
			// Take the first available work item.
			struct mm_work *work = mm_core_get_work(core);

			// Start a new worker to handle the work.
			struct mm_task_attr attr;
			mm_task_attr_init(&attr);
			mm_task_attr_setpriority(&attr, MM_PRIO_WORKER);
			mm_task_attr_setname(&attr, "worker");
			mm_task_create(&attr, mm_core_worker, (mm_value_t) work);
			core->nworkers++;
		}

		// Wait for work at the back end of the idle queue.
		// So any idle worker would take work before the master.
		mm_core_idle(core, true);
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Dealer task.
 **********************************************************************/

// Dealer loop sleep time - 1 second
#define MM_DEALER_HALT_TIMEOUT	((mm_timeout_t) 1000000)

// Dealer loop hang on times
#define MM_DEALER_POLL_TIMEOUT	((mm_timeout_t) 10)
#define MM_DEALER_HOLD_TIMEOUT	((mm_timeout_t) 25)

static mm_atomic_uint32_t mm_core_deal_count;

static void
mm_core_deal(struct mm_core *core)
{
	ENTER();

	// Start current timer tasks.
	mm_timer_tick();

	// Indicate that the data that has so far been put to the rings
	// is to be consumed now.
	mm_synch_clear(core->synch);

	// Consume the data from the communication rings.
	mm_core_destroy_chunks(core);
	mm_core_receive_tasks(core);
	mm_core_receive_work(core);

	// Run the pending tasks.
	mm_task_yield();

	// Cleanup the temporary data.
	mm_wait_cache_truncate(&core->wait_cache);

	mm_atomic_uint32_inc(&mm_core_deal_count);

	LEAVE();
}

static void
mm_core_halt(struct mm_core *core)
{
	ENTER();

	// Get the closest pending timer.
	mm_timeval_t wait_time = mm_timer_next();

	// Consider the time until the next required event poll.
	mm_timeval_t poll_time = MM_TIMEVAL_MAX;
	if (core->events != NULL) {
		// If any event system changes have been requested then it is
		// required to notify on their completion immediately.
		if (mm_event_collect(core->events))
			poll_time = wait_time = core->time_value;
		else
			poll_time = core->poll_time + MM_DEALER_POLL_TIMEOUT;
	}

	// Find the halt time.
	mm_timeval_t halt_time = min(wait_time, core->time_value + MM_DEALER_HALT_TIMEOUT);

#if 1
	mm_core_update_time(core);
#else
	// Before actually halting hang around a little bit waiting for
	// possible wake requests.
	mm_timeval_t hold_time = min(wait_time, core->time_value + MM_DEALER_HOLD_TIMEOUT);
	if (hold_time > poll_time)
		hold_time = poll_time;
	if (core->time_value < hold_time) {
		bool quit = false;
		while (!quit) {
			mm_core_update_time(core);
			if (core->time_value >= hold_time)
				break;
			for (int i = 0; i < 10; i++) {
				quit = mm_synch_test(core->synch);
				if (quit) {
					halt_time = core->time_value;
					break;
				}
				mm_spin_pause();
			}
		}
	}
#endif

	mm_timeout_t timeout = 0;
	if (core->time_value < halt_time)
		timeout = halt_time - core->time_value;

	if (timeout || core->time_value >= poll_time) {
		bool dispatch = mm_synch_timedwait(core->synch, timeout);
		mm_core_update_time(core);
		if (core->events != NULL) {
			core->poll_time = core->time_value;
			if (dispatch)
				mm_event_dispatch(core->events);
		}
	}

	LEAVE();
}

static mm_value_t
mm_core_dealer(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;

	while (!mm_memory_load(core->stop)) {
		mm_core_deal(core);
		mm_core_halt(core);
	}

	LEAVE();
	return 0;
}

void
mm_core_stats(void)
{
	uint32_t deal = mm_memory_load(mm_core_deal_count);
	mm_verbose("core stats: deal = %u", deal);
	mm_event_stats();
	mm_lock_stats();
}

/**********************************************************************
 * Core start and stop hooks.
 **********************************************************************/

static struct mm_queue MM_QUEUE_INIT(mm_core_start_hook);
static struct mm_queue MM_QUEUE_INIT(mm_core_stop_hook);

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

	mm_hook_tail_data_proc(&mm_core_start_hook, proc, data);

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

	mm_hook_tail_data_proc(&mm_core_stop_hook, proc, data);

	LEAVE();
}

/**********************************************************************
 * Core initialization and termination.
 **********************************************************************/

static void
mm_core_boot_signal(void)
{
#if ENABLE_SMP
	for (mm_core_t i = 1; i < mm_core_num; i++) {
		mm_synch_signal(mm_core_set[i].synch);
	}
#endif
}

static void
mm_core_boot_join(void)
{
#if ENABLE_SMP
	for (mm_core_t i = 1; i < mm_core_num; i++) {
		struct mm_thread *thread = NULL;
		do
			thread = mm_memory_load(mm_core_set[i].thread);
		while (thread == NULL);

		mm_thread_join(thread);
	}
#endif
}

static void
mm_core_boot_init(struct mm_core *core)
{
	// Secondary cores have to wait until the primary core runs
	// the start hooks that initialize shared resources.
	if (!MM_CORE_IS_PRIMARY(core))
		mm_synch_wait(core->synch);

	mm_timer_init();
	mm_future_init();

	// Update the time.
	mm_core_update_time(core);
	mm_core_update_real_time(core);

	// Create the time queue.
	core->time_queue = mm_timeq_create();
	mm_timeq_set_max_bucket_width(core->time_queue, MM_TIME_QUEUE_MAX_WIDTH);
	mm_timeq_set_max_bucket_count(core->time_queue, MM_TIME_QUEUE_MAX_COUNT);

	// Call the start hooks on the primary core.
	if (MM_CORE_IS_PRIMARY(core)) {
		mm_hook_call(&mm_core_start_hook, false);
		mm_cdata_summary();
		mm_core_boot_signal();
	}
}

static void
mm_core_boot_term(struct mm_core *core)
{
	// Call the stop hooks on the primary core.
	if (MM_CORE_IS_PRIMARY(core)) {
		mm_core_boot_join();
		mm_hook_call(&mm_core_stop_hook, false);
	}

	mm_timeq_destroy(core->time_queue);

	mm_future_term();
	mm_timer_term();

	// TODO:
	//mm_task_destroy(core->master);
	//mm_task_destroy(core->dealer);
}

static void
mm_core_start_basic_tasks(struct mm_core *core)
{
	struct mm_task_attr attr;
	mm_task_attr_init(&attr);

	// Create the master task for this core and schedule it for execution.
	mm_task_attr_setpriority(&attr, MM_PRIO_MASTER);
	mm_task_attr_setname(&attr, "master");
	core->master = mm_task_create(&attr, mm_core_master, (mm_value_t) core);

	// Create the dealer task for this core and schedule it for execution.
	mm_task_attr_setpriority(&attr, MM_PRIO_DEALER);
	mm_task_attr_setname(&attr, "dealer");
	core->dealer = mm_task_create(&attr, mm_core_dealer, (mm_value_t) core);
}

/* A per-core thread entry point. */
static mm_value_t
mm_core_boot(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;

	// Set the thread-local pointer to the core object.
	mm_core = core;
	mm_core->thread = mm_thread_self();

	// Set the thread-local pointer to the running task.
	mm_running_task = mm_core->boot;
	mm_running_task->state = MM_TASK_RUNNING;

	// Initialize per-core resources.
	mm_core_boot_init(core);

	// Start master & dealer tasks.
	mm_core_start_basic_tasks(core);

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
	mm_queue_init(&core->workq);

	mm_wait_cache_prepare(&core->wait_cache);

	core->nwork = 0;
	core->nidle = 0;
	core->nworkers = 0;
	core->nworkers_max = nworkers_max;

	core->time_queue = NULL;
	core->time_value = 0;
	core->real_time_value = 0;

	core->master = NULL;
	core->thread = NULL;

	core->events = NULL;
	core->poll_time = 0;

	core->stop = false;
	core->synch = NULL;

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
mm_core_term_work(struct mm_core *core)
{
	while (mm_core_has_work(core)) {
		struct mm_work *work = mm_core_get_work(core);
		mm_core_destroy_work(core, work);
	}
}

static void
mm_core_term_inbox(struct mm_core *core)
{
	struct mm_work *work = mm_ring_get(&core->inbox);
	while (work != NULL) {
		mm_core_destroy_work(core, work);
		work = mm_ring_get(&core->inbox);
	}
}

static void
mm_core_term_single(struct mm_core *core)
{
	ENTER();

	mm_core_term_work(core);
	mm_core_term_inbox(core);

	mm_wait_cache_cleanup(&core->wait_cache);

	mm_synch_destroy(core->synch);
	if (core->events != NULL)
		mm_event_destroy_table(core->events);

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
mm_core_get_ncpu(void)
{
#if ENABLE_SMP
# if defined(HAVE_SYS_SYSCTL_H) && defined(HW_AVAILCPU)
//#  define SELECTOR "hw.ncpu"
#  define SELECTOR "hw.activecpu"
//#  define SELECTOR "hw.physicalcpu"
	int num;
	size_t len = sizeof num;
	if (sysctlbyname(SELECTOR, &num, &len, NULL, 0) < 0)
		mm_fatal(errno, "Failed to count cores.");
	return num;
# elif defined(_SC_NPROCESSORS_ONLN)
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

	// Find the number of CPU cores.
	mm_core_num = mm_core_get_ncpu();
	ASSERT(mm_core_num > 0);
	if (mm_core_num == 1)
		mm_brief("Running on 1 core.");
	else
		mm_brief("Running on %d cores.", mm_core_num);
	mm_bitset_prepare(&mm_core_event_affinity, &mm_alloc_global, mm_core_num);

	mm_alloc_init();
	mm_cdata_init();

	mm_clock_init();
	mm_thread_init();
	mm_event_init();
	mm_net_init();

	mm_task_init();
	mm_port_init();
	mm_wait_init();
	mm_core_init_work();

	mm_core_set = mm_global_alloc_aligned(MM_CACHELINE, mm_core_num * sizeof(struct mm_core));
	for (mm_core_t i = 0; i < mm_core_num; i++)
		mm_core_init_single(&mm_core_set[i], MM_DEFAULT_WORKERS);

	LEAVE();
}

void
mm_core_term(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	mm_bitset_cleanup(&mm_core_event_affinity, &mm_alloc_global);

	for (mm_core_t i = 0; i < mm_core_num; i++)
		mm_core_term_single(&mm_core_set[i]);
	mm_global_free(mm_core_set);

	mm_core_free_hooks();

	mm_task_term();
	mm_port_term();
	mm_wait_term();

	mm_net_term();
	mm_event_term();
	mm_thread_term();

	mm_cdata_term();
	mm_alloc_term();

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
mm_core_set_event_affinity(const struct mm_bitset *mask)
{
	ENTER();

	mm_bitset_clear_all(&mm_core_event_affinity);
	mm_bitset_or(&mm_core_event_affinity, mask);

	LEAVE();
}

const struct mm_bitset *
mm_core_get_event_affinity(void)
{
	return &mm_core_event_affinity;
}

void
mm_core_start(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	// Set up event loops and synchronization.
	if (!mm_bitset_any(&mm_core_event_affinity))
		mm_bitset_set(&mm_core_event_affinity, 0);
	for (mm_core_t i = 0; i < mm_core_num; i++) {
		struct mm_core *core = &mm_core_set[i];
		if (mm_bitset_test(&mm_core_event_affinity, i)) {
			mm_brief("start event loop on core %d", i);
			core->events = mm_event_create_table();
			core->synch = mm_synch_create_event_poll(core->events);
		} else {
			core->synch = mm_synch_create();
		}
	}

	// Start core threads.
	for (mm_core_t i = 0; i < mm_core_num; i++)
		mm_core_start_single(&mm_core_set[i], i);

	// Loop until stopped.
	while (!mm_exit_test()) {
		size_t logged = mm_log_flush();

		DEBUG("cycle");
		mm_core_stats();
		mm_log_relay();
		mm_log_flush();

		usleep(logged ? 30000 : 3000000);
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
	for (mm_core_t i = 0; i < mm_core_num; i++) {
		struct mm_core *core = &mm_core_set[i];
		mm_memory_store(core->stop, true);
		mm_synch_signal(core->synch);
	}

	LEAVE();
}
