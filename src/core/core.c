/*
 * core/core.c - MainMemory core.
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

#include "core/core.h"
#include "core/future.h"
#include "core/port.h"
#include "core/task.h"
#include "core/work.h"

#include "base/base.h"
#include "base/bitset.h"
#include "base/exit.h"
#include "base/logger.h"
#include "base/event/dispatch.h"
#include "base/memory/chunk.h"
#include "base/memory/global.h"
#include "base/thread/domain.h"
#include "base/thread/local.h"
#include "base/thread/thread.h"
#include "base/util/hook.h"

#include "net/net.h"

#include <stdio.h>

#define MM_NWORKERS_MIN		2
#define MM_NWORKERS_MAX		256

#if ENABLE_SMP
# define MM_CORE_IS_PRIMARY(core)	(core == mm_core_set)
#else
# define MM_CORE_IS_PRIMARY(core)	(true)
#endif

// The core set.
mm_core_t mm_core_num;
struct mm_core *mm_core_set;

// A core associated with the running thread.
__thread struct mm_core *__mm_core_self;

// The set of cores with event loops.
static struct mm_bitset mm_core_event_affinity;

// Common event dispatch.
static struct mm_event_dispatch mm_core_dispatch;

/**********************************************************************
 * Yield routine for backoff on busy waiting.
 **********************************************************************/

#if ENABLE_TASK_LOCATION
static void
mm_core_relax(void)
{
	mm_task_yield();
}
#endif

static void
mm_core_enable_yield(struct mm_core *core)
{
#if ENABLE_TASK_LOCATION
	mm_thread_setrelax(core->thread, mm_core_relax);
#else
	mm_thread_setrelax(core->thread, mm_task_yield);
#endif
}

static void
mm_core_disable_yield(struct mm_core *core)
{
	mm_thread_setrelax(core->thread, NULL);
}

/**********************************************************************
 * Idle queue.
 **********************************************************************/

void
mm_core_idle(struct mm_core *core, bool tail)
{
	ENTER();

	struct mm_task *task = core->task;
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
		struct mm_link *link = mm_list_head(&core->idle);
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
	struct mm_qlink *link = mm_queue_remove(&core->workq);
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

#if ENABLE_SMP

static void
mm_core_post_work_req(uintptr_t *arguments)
{
	ENTER();

	struct mm_work *work = (struct mm_work *) arguments[0];
	mm_core_add_work(mm_core_selfptr(), work);

	LEAVE();
}

void NONNULL(2)
mm_core_post_work(mm_core_t core_id, struct mm_work *work)
{
	ENTER();

	// Get the target core.
	struct mm_core *core = mm_core_getptr(core_id);

	// Dispatch the work item.
	if (core == mm_core_selfptr()) {
		// Enqueue it directly if on the same core.
		mm_core_add_work(core, work);
	} else if (core == NULL) {
		// Submit it to the domain request queue.
		struct mm_domain *domain = mm_domain_selfptr();
		mm_domain_post_1(domain, mm_core_post_work_req, (uintptr_t) work);
		mm_domain_notify(domain);
	} else {
		// Submit it to the thread request queue.
		struct mm_thread *thread = core->thread;
		mm_thread_post_1(thread, mm_core_post_work_req, (uintptr_t) work);
	}

	LEAVE();
}

#else

void
mm_core_post_work(mm_core_t core_id, struct mm_work *work)
{
	ENTER();

	(void) core_id;
	mm_core_add_work(mm_core_selfptr(), work);

	LEAVE();
}

#endif

void NONNULL(2)
mm_core_post(mm_core_t core_id, mm_routine_t routine, mm_value_t routine_arg)
{
	ENTER();

	// Create a work item.
	struct mm_work *work = mm_work_create();
	mm_work_prepare(work, routine, routine_arg, NULL);

	// Post it to specified core.
	mm_core_post_work(core_id, work);

	LEAVE();
}

/**********************************************************************
 * Task queue.
 **********************************************************************/

#if ENABLE_SMP
static void
mm_core_run_task_req(uintptr_t *arguments)
{
	ENTER();

	struct mm_task *task = (struct mm_task *) arguments[0];
	mm_task_run(task);

	LEAVE();
}
#endif

void NONNULL(1)
mm_core_run_task(struct mm_task *task)
{
	ENTER();

#if ENABLE_SMP
	if (task->core == mm_core_selfptr()) {
		// Put the task to the core run queue directly.
		mm_task_run(task);
	} else {
		// Submit the task to the thread request queue.
		struct mm_thread *thread = task->core->thread;
		mm_thread_post_1(thread, mm_core_run_task_req, (uintptr_t) task);
	}
#else
	mm_task_run(task);
#endif

	LEAVE();
}

/**********************************************************************
 * Worker task.
 **********************************************************************/

static void
mm_core_worker_cancel(uintptr_t arg)
{
	ENTER();

	// Notify that the work has been canceled.
	struct mm_work *work = (struct mm_work *) arg;
	work->complete(work, MM_RESULT_CANCELED);

	LEAVE();
}

static void
mm_core_worker_execute(struct mm_work *work)
{
	ENTER();

	// Save the work data before it might be destroyed.
	mm_routine_t routine = work->routine;
	mm_value_t value = work->argument;
	mm_work_complete_t complete = work->complete;

	if (complete == NULL) {
		// Destroy unneeded work data.
		mm_work_destroy(work);
		// Execute the work routine.
		routine(value);
	} else {
		// Ensure completion notification on task cancellation.
		mm_task_cleanup_push(mm_core_worker_cancel, work);

		// Execute the work routine.
		value = routine(value);

		// Task completed, no cleanup is required.
		mm_task_cleanup_pop(false);

		// Perform completion notification on return.
		complete(work, value);
	}

	LEAVE();
}

static void
mm_core_worker_cleanup(uintptr_t arg UNUSED)
{
	struct mm_core *core = mm_core_selfptr();

	// Wake up the master possibly waiting for worker availability.
	if (core->nworkers == core->nworkers_max)
		mm_task_run(core->master);

	// Account for the exiting worker.
	core->nworkers--;
}

static mm_value_t
mm_core_worker(mm_value_t arg)
{
	ENTER();

	// Ensure cleanup on exit.
	mm_task_cleanup_push(mm_core_worker_cleanup, 0);

	// TODO: verify this again, perhaps it was a dumb compiler
	// Cache thread-specific data. This gives a smallish speedup for
	// the code emitted for the loop below on platforms with emulated
	// thread specific data (that is on Darwin).
	struct mm_core *core = mm_core_selfptr();

	// Take the work item supplied by the master.
	struct mm_work *work = (struct mm_work *) arg;
	for (;;) {
		mm_core_worker_execute(work);

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
mm_core_nowork(mm_value_t arg)
{
	return arg;
}

static mm_value_t
mm_core_master(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;
	bool verbose = mm_get_verbose_enabled();

	// Force creation of the minimal number of workers.
	while (core->nwork < core->nworkers_min)
		mm_core_post(mm_core_getid(core), mm_core_nowork, 0);

	while (!mm_memory_load(core->stop)) {

		// Inform about the status of all tasks.
		if (verbose && core->nworkers > core->nworkers_min && mm_core_has_work(core))
			mm_core_print_tasks(core);

		// Check to see if there are enough workers.
		if (core->nworkers >= core->nworkers_max) {
			mm_task_block();
			continue;
		}

		// Check to see if there is outstanding work.
		if (mm_core_has_work(core)) {
			// Take the first available work item.
			struct mm_work *work = mm_core_get_work(core);

			// Make a unique worker name.
			char worker_name[32];
			static mm_atomic_uint32_t nworkers;
			uint32_t id = mm_atomic_uint32_fetch_and_add(&nworkers, 1);
			sprintf(worker_name, "worker %u", (uint32_t) id);

			// Make a new worker task to handle it.
			struct mm_task_attr attr;
			mm_task_attr_init(&attr);
			mm_task_attr_setpriority(&attr, MM_PRIO_WORKER);
			mm_task_attr_setname(&attr, worker_name);
			mm_task_create(&attr, mm_core_worker, (mm_value_t) work);
			core->nworkers++;
		} else {
			// Wait for work at the back end of the idle queue.
			// So any idle worker would take work before the master.
			mm_core_idle(core, true);
		}
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Dealer task.
 **********************************************************************/

// Dealer loop sleep time - 10 seconds
#define MM_CORE_HALT_TIMEOUT	((mm_timeout_t) 10 * 1000 * 1000)

void NONNULL(1)
mm_core_execute_requests(struct mm_core *core)
{
	ENTER();
	struct mm_request_data request;

	struct mm_thread *const thread = core->thread;
	while (mm_thread_receive(thread, &request)) {
		mm_request_execute(&request);
		core->thread_request_count++;
	}

#if ENABLE_SMP
	struct mm_domain *const domain = mm_thread_getdomain(thread);
	while (mm_runq_empty_above(&core->runq, MM_PRIO_IDLE)
	       && mm_domain_receive(domain, &request)) {
		mm_request_execute(&request);
		core->domain_request_count++;
	}
#endif

	LEAVE();
}

static void
mm_core_trim(struct mm_core *core)
{
	ENTER();

	// Cleanup the temporary data.
	mm_wait_cache_truncate(&core->wait_cache);
	mm_chunk_enqueue_deferred(core->thread, true);

#if ENABLE_SMP
	// Trim private memory space.
	mm_private_space_trim(mm_thread_getspace(core->thread));
#endif

	LEAVE();
}

static void
mm_core_halt(struct mm_core *core)
{
	ENTER();

	// Get the closest expiring timer if any.
	mm_timeval_t wake_time = mm_timer_next(&core->time_manager);
	if (wake_time != MM_TIMEVAL_MAX) {
		// Calculate the timeout.
		mm_timeout_t timeout = MM_CORE_HALT_TIMEOUT;
		mm_timeval_t time = mm_core_gettime(core);
		if (wake_time < (time + timeout)) {
			if (wake_time > time)
				timeout = wake_time - time;
			else
				timeout = 0;
		}

		// Halt the core waiting for incoming events.
		mm_event_listen(mm_thread_getlistener(core->thread), timeout);

		// Indicate that clocks need to be updated.
		mm_timer_resetclocks(&core->time_manager);

		// Fire reached timers.
		mm_timer_tick(&core->time_manager);

	} else {
		// Halt the core waiting for incoming events.
		mm_event_listen(mm_thread_getlistener(core->thread), MM_CORE_HALT_TIMEOUT);

		// Indicate that clocks need to be updated.
		mm_timer_resetclocks(&core->time_manager);
	}

	LEAVE();
}

static mm_value_t
mm_core_dealer(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;

	while (!mm_memory_load(core->stop)) {

		// Count the loop cycles.
		core->loop_count++;

		// Run the queued tasks if any.
		mm_task_yield();

		// Release excessive resources allocated by tasks.
		mm_core_trim(core);

		// Enter the state that forbids task switches.
		core->state = MM_CORE_WAITING;
		// Halt waiting for incoming requests.
		mm_core_halt(core);
		// Restore normal running state.
		core->state = MM_CORE_RUNNING;
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Core diagnostics and statistics.
 **********************************************************************/

static void
mm_core_print_task_list(struct mm_list *list)
{
	struct mm_link *link = &list->base;
	while (!mm_list_is_tail(list, link)) {
		link = link->next;
		struct mm_task *task = containerof(link, struct mm_task, queue);
		mm_task_print_status(task);
	}
}

void NONNULL(1)
mm_core_print_tasks(struct mm_core *core)
{
	mm_brief("tasks on core %d (#idle=%u, #work=%u):",
		 mm_core_getid(core), core->nidle, core->nwork);
	for (int i = 0; i < MM_RUNQ_BINS; i++)
		mm_core_print_task_list(&core->runq.bins[i]);
	mm_core_print_task_list(&core->block);
}

void
mm_core_stats(void)
{
	mm_core_t n = mm_core_getnum();
	for (mm_core_t i = 0; i < n; i++) {
		struct mm_core *core = mm_core_getptr(i);
		mm_verbose("core %d: cycles=%llu, cswitches=%llu/%llu/%llu,"
			   " requests=%llu/%llu, workers=%lu", i,
			   (unsigned long long) core->loop_count,
			   (unsigned long long) core->cswitch_count,
			   (unsigned long long) core->cswitch_denied_in_waiting_state,
			   (unsigned long long) core->cswitch_denied_in_cswitch_state,
			   (unsigned long long) core->thread_request_count,
#if ENABLE_SMP
			   (unsigned long long) core->domain_request_count,
#else
			   (unsigned long long) 0,
#endif
			   (unsigned long) core->nworkers);
	}

	mm_event_dispatch_stats(&mm_core_dispatch);
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
 * Core Initialization and Termination.
 **********************************************************************/

static void
mm_core_boot_init(struct mm_core *core)
{
	struct mm_private_space *space = mm_private_space_get();
	if (MM_CORE_IS_PRIMARY(core)) {
		struct mm_domain *domain = mm_domain_selfptr();

		mm_timer_prepare(&core->time_manager, &space->xarena);

		// Call the start hooks on the primary core.
		mm_hook_call(&mm_core_start_hook, false);
		mm_thread_local_summary(domain);

		mm_event_dispatch_prepare(&mm_core_dispatch, domain,
					  domain->nthreads, domain->threads);

		mm_thread_domain_barrier();
	} else {
		// Secondary cores have to wait until the primary core runs
		// the start hooks that initialize shared resources.
		mm_thread_domain_barrier();

		mm_timer_prepare(&core->time_manager, &space->xarena);
	}
}

static void
mm_core_boot_term(struct mm_core *core)
{
	mm_thread_domain_barrier();

	// Call the stop hooks on the primary core.
	if (MM_CORE_IS_PRIMARY(core)) {
		mm_hook_call(&mm_core_stop_hook, false);
		mm_event_dispatch_cleanup(&mm_core_dispatch);
	}

	mm_timer_cleanup(&core->time_manager);

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

	struct mm_core *core = &mm_core_set[arg];
	core->thread = mm_thread_selfptr();

	// Set the thread-specific data.
	__mm_core_self = core;

	// Set pointer to the running task.
	core->task = core->boot;
	core->task->state = MM_TASK_RUNNING;

#if ENABLE_TRACE
	mm_trace_context_prepare(&core->task->trace, "[%s][%d %s]",
				 mm_thread_getname(core->thread),
				 mm_task_getid(core->task),
				 mm_task_getname(core->task));
#endif

	// Initialize per-core resources.
	mm_core_boot_init(core);

	// Start master & dealer tasks.
	mm_core_start_basic_tasks(core);

	// Enable yielding to other tasks on busy waiting.
	mm_core_enable_yield(core);

	// Run the other tasks while there are any.
	core->state = MM_CORE_RUNNING;
	mm_task_yield();
	core->state = MM_CORE_INVALID;

	// Disable yielding to other tasks.
	mm_core_disable_yield(core);

	// Destroy per-core resources.
	mm_core_boot_term(core);

	// Invalidate the boot task.
	core->task->state = MM_TASK_INVALID;
	core->task = NULL;

	// Abandon the core.
	__mm_core_self = NULL;

	LEAVE();
	return 0;
}

static void
mm_core_init_single(struct mm_core *core)
{
	ENTER();

	mm_runq_prepare(&core->runq);
	mm_list_prepare(&core->idle);
	mm_list_prepare(&core->dead);
	mm_list_prepare(&core->block);
	mm_list_prepare(&core->async);
	mm_queue_prepare(&core->workq);

	mm_wait_cache_prepare(&core->wait_cache);

	core->state = MM_CORE_INVALID;

	core->nwork = 0;
	core->nidle = 0;
	core->nworkers = 0;
	core->nworkers_min = MM_NWORKERS_MIN;
	core->nworkers_max = MM_NWORKERS_MAX;

	core->cswitch_count = 0;
	core->cswitch_denied_in_cswitch_state = 0;
	core->cswitch_denied_in_waiting_state = 0;

	core->thread_request_count = 0;
#if ENABLE_SMP
	core->domain_request_count = 0;
#endif

	core->master = NULL;
	core->dealer = NULL;

	core->thread = NULL;

	core->stop = false;

	// Create the core bootstrap task.
	struct mm_task_attr attr;
	mm_task_attr_init(&attr);
	mm_task_attr_setflags(&attr, MM_TASK_BOOT | MM_TASK_CANCEL_DISABLE);
	mm_task_attr_setpriority(&attr, MM_PRIO_BOOT);
	mm_task_attr_setname(&attr, "boot");
	core->boot = mm_task_create(&attr, mm_core_boot, (mm_value_t) core);

	LEAVE();
}

static void
mm_core_term_work(struct mm_core *core)
{
	mm_core_t core_id = mm_core_getid(core);
	while (mm_core_has_work(core)) {
		struct mm_work *work = mm_core_get_work(core);
		mm_work_destroy_low(core_id, work);
	}
}

static void
mm_core_term_single(struct mm_core *core)
{
	ENTER();

	mm_core_term_work(core);
	mm_wait_cache_cleanup(&core->wait_cache);

	mm_task_destroy(core->boot);

	// Flush logs before memory space with possible log chunks is unmapped.
	mm_log_relay();
	mm_log_flush();

	LEAVE();
}

#if ENABLE_TRACE

static struct mm_trace_context *
mm_core_gettracecontext(void)
{
	struct mm_core *core = mm_core_selfptr();
	if (core != NULL)
		return &core->task->trace;
	struct mm_thread *thread = mm_thread_selfptr();
	if (unlikely(thread == NULL))
		ABORT();
	return mm_thread_gettracecontext(thread);
}

#endif

void
mm_core_init(void)
{
	ENTER();
	ASSERT(mm_core_num == 0);

	mm_base_init();

	// Find the number of CPU cores.
	mm_core_num = mm_ncpus;
	if (mm_core_num == 1)
		mm_brief("running on 1 core.");
	else
		mm_brief("running on %d cores.", mm_core_num);

	mm_net_init();

	mm_task_init();
	mm_port_init();
	mm_wait_init();
	mm_future_init();
	mm_work_init();

#if ENABLE_TRACE
	mm_trace_set_getcontext(mm_core_gettracecontext);
#endif

	mm_core_set = mm_global_aligned_alloc(MM_CACHELINE, mm_core_num * sizeof(struct mm_core));
	for (mm_core_t i = 0; i < mm_core_num; i++)
		mm_core_init_single(&mm_core_set[i]);

	mm_bitset_prepare(&mm_core_event_affinity, &mm_common_space.xarena, mm_core_num);

	LEAVE();
}

void
mm_core_term(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	mm_bitset_cleanup(&mm_core_event_affinity, &mm_common_space.xarena);

	for (mm_core_t i = 0; i < mm_core_num; i++)
		mm_core_term_single(&mm_core_set[i]);
	mm_global_free(mm_core_set);

	mm_core_free_hooks();

	mm_task_term();
	mm_port_term();
	mm_wait_term();

	mm_net_term();

	mm_base_term();

	LEAVE();
}

void NONNULL(1)
mm_core_register_server(struct mm_net_server *srv)
{
	ENTER();

	// Register the server start hook.
	mm_core_hook_param_start((mm_hook_rtn1) mm_net_start_server, srv);

	// Register the server stop hook.
	mm_core_hook_param_stop((mm_hook_rtn1) mm_net_stop_server, srv);

	LEAVE();
}

void NONNULL(1)
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

	// Set the base library params.
	struct mm_base_params params = {
		.regular_name = "core",
		.thread_stack_size = MM_PAGE_SIZE,
		.thread_guard_size = MM_PAGE_SIZE,
		.thread_routine = mm_core_boot,
	};

	// Run core threads.
	mm_base_loop(&params);

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
		mm_thread_wakeup(core->thread);
	}

	LEAVE();
}
