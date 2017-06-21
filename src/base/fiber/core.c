/*
 * base/fiber/core.c - MainMemory core.
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

#include "base/fiber/core.h"

#include "base/bitset.h"
#include "base/exit.h"
#include "base/logger.h"
#include "base/event/dispatch.h"
#include "base/fiber/fiber.h"
#include "base/fiber/future.h"
#include "base/fiber/work.h"
#include "base/memory/chunk.h"
#include "base/memory/global.h"
#include "base/memory/memory.h"
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
struct mm_core *mm_core_set;

// A core associated with the running thread.
__thread struct mm_core *__mm_core_self;

/**********************************************************************
 * Yield routine for backoff on busy waiting.
 **********************************************************************/

#if ENABLE_FIBER_LOCATION
static void
mm_core_relax(void)
{
	mm_fiber_yield();
}
#endif

static void
mm_core_enable_yield(struct mm_core *core)
{
#if ENABLE_FIBER_LOCATION
	mm_thread_setrelax(core->thread, mm_core_relax);
#else
	mm_thread_setrelax(core->thread, mm_fiber_yield);
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

	// Put the fiber into the wait queue.
	struct mm_fiber *fiber = core->fiber;
	if (tail)
		mm_list_append(&core->idle, &fiber->wait_queue);
	else
		mm_list_insert(&core->idle, &fiber->wait_queue);

	ASSERT((fiber->flags & MM_FIBER_WAITING) == 0);
	fiber->flags |= MM_FIBER_WAITING;
	core->nidle++;

	// Wait until poked.
	mm_fiber_block();

	// Normally an idle fiber starts after being poked and
	// in this case it should already be removed from the
	// wait list. But if the fiber has started for another
	// reason it must be removed from the wait list here.
	if (unlikely((fiber->flags & MM_FIBER_WAITING) != 0)) {
		mm_list_delete(&fiber->wait_queue);
		fiber->flags &= ~MM_FIBER_WAITING;
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
		struct mm_fiber *fiber = containerof(link, struct mm_fiber, wait_queue);

		// Get a fiber from the wait queue.
		ASSERT((fiber->flags & MM_FIBER_WAITING) != 0);
		mm_list_delete(&fiber->wait_queue);
		fiber->flags &= ~MM_FIBER_WAITING;
		core->nidle--;

		// Put the fiber to the run queue.
		mm_fiber_run(fiber);
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
	// Enqueue the work item.
	mm_queue_append(&core->workq, &work->link);
	core->nwork++;

	// If there is a fiber waiting for work then let it run now.
	mm_core_poke(core);
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
mm_core_post_work(mm_thread_t core_id, struct mm_work *work)
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
mm_core_post_work(mm_thread_t core_id, struct mm_work *work)
{
	ENTER();

	(void) core_id;
	mm_core_add_work(mm_core_selfptr(), work);

	LEAVE();
}

#endif

/**********************************************************************
 * Fiber queue.
 **********************************************************************/

#if ENABLE_SMP
static void
mm_core_run_fiber_req(uintptr_t *arguments)
{
	ENTER();

	struct mm_fiber *fiber = (struct mm_fiber *) arguments[0];
	mm_fiber_run(fiber);

	LEAVE();
}
#endif

void NONNULL(1)
mm_core_run_fiber(struct mm_fiber *fiber)
{
	ENTER();

#if ENABLE_SMP
	if (fiber->core == mm_core_selfptr()) {
		// Put the fiber to the core run queue directly.
		mm_fiber_run(fiber);
	} else {
		// Submit the fiber to the thread request queue.
		struct mm_thread *thread = fiber->core->thread;
		mm_thread_post_1(thread, mm_core_run_fiber_req, (uintptr_t) fiber);
	}
#else
	mm_fiber_run(fiber);
#endif

	LEAVE();
}

/**********************************************************************
 * Worker fiber.
 **********************************************************************/

static void
mm_core_worker_cleanup(uintptr_t arg)
{
	struct mm_core *core = mm_core_selfptr();
	struct mm_work *work = *((struct mm_work **) arg);

	// Notify that the current work has been canceled.
	if (work != NULL)
		(work->vtable->complete)(work, MM_RESULT_CANCELED);

	// Wake up the master possibly waiting for worker availability.
	if (core->nworkers == core->nworkers_max)
		mm_fiber_run(core->master);

	// Account for the exiting worker.
	core->nworkers--;
}

static mm_value_t
mm_core_worker(mm_value_t arg)
{
	ENTER();

	// The work item to cancel.
	struct mm_work *cancel = NULL;

	// Ensure cleanup on exit.
	mm_fiber_cleanup_push(mm_core_worker_cleanup, &cancel);

	// Handle the work item supplied by the master.
	if (arg) {
		struct mm_work *work = (struct mm_work *) arg;
		const struct mm_work_vtable *vtable = work->vtable;
		// Execute the work routine.
		mm_memory_store(cancel, work);
		mm_value_t result = (vtable->routine)(work);
		mm_memory_store(cancel, NULL);
		// Perform completion notification on return.
		(vtable->complete)(work, result);
	}

	struct mm_core *const core = mm_core_selfptr();
	for (;;) {
		// Wait for work standing at the front of the idle queue.
		while (!mm_core_has_work(core))
			mm_core_idle(core, false);

		// Handle the first available work item.
		struct mm_work *work = mm_core_get_work(core);
		const struct mm_work_vtable *vtable = work->vtable;
		// Execute the work routine.
		mm_memory_store(cancel, work);
		mm_value_t result = (vtable->routine)(work);
		mm_memory_store(cancel, NULL);
		// Perform completion notification on return.
		(vtable->complete)(work, result);
	}

	// Cleanup on return.
	mm_fiber_cleanup_pop(true);

	LEAVE();
	return 0;
}

static void
mm_core_worker_create(struct mm_core *core, mm_value_t arg)
{
	ENTER();

	// Make a unique worker name.
	char name[MM_FIBER_NAME_SIZE];
	snprintf(name, MM_FIBER_NAME_SIZE, "worker %u:%u", mm_core_getid(core), core->nworkers);

	// Make a new worker fiber and start it.
	struct mm_fiber_attr attr;
	mm_fiber_attr_init(&attr);
	mm_fiber_attr_setpriority(&attr, MM_PRIO_WORKER);
	mm_fiber_attr_setname(&attr, name);
	mm_fiber_create(&attr, mm_core_worker, arg);

	// Account for the newcomer worker.
	core->nworkers++;

	LEAVE();
}

/**********************************************************************
 * Master fiber.
 **********************************************************************/

static mm_value_t
mm_core_master(mm_value_t arg)
{
	ENTER();

	struct mm_core *core = (struct mm_core *) arg;
	bool verbose = mm_get_verbose_enabled();

	// Force creation of the minimal number of workers.
	while (core->nworkers < core->nworkers_min)
		mm_core_worker_create(core, 0);

	while (!mm_memory_load(core->stop)) {
		// Check to see if there are enough workers.
		if (core->nworkers >= core->nworkers_max) {
			mm_fiber_block();
			continue;
		}

		// Wait for work at the back end of the idle queue.
		// So any idle worker would take work before the master.
		mm_core_idle(core, true);

		// Check to see if there is outstanding work.
		if (mm_core_has_work(core)) {
			// Take the first available work item.
			struct mm_work *work = mm_core_get_work(core);

			// Make a new worker fiber to handle it.
			mm_core_worker_create(core, (mm_value_t) work);

			// Inform about the status of all fibers.
			if (verbose)
				mm_core_print_fibers(core);
		}
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Dealer fiber.
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

		// Run the queued fibers if any.
		mm_fiber_yield();

		// Release excessive resources allocated by fibers.
		mm_core_trim(core);

		// Enter the state that forbids fiber switches.
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
mm_core_print_fiber_list(struct mm_list *list)
{
	struct mm_link *link = &list->base;
	while (!mm_list_is_tail(list, link)) {
		link = link->next;
		struct mm_fiber *fiber = containerof(link, struct mm_fiber, queue);
		mm_fiber_print_status(fiber);
	}
}

void NONNULL(1)
mm_core_print_fibers(struct mm_core *core)
{
	mm_brief("fibers on core %d (#idle=%u, #work=%u):",
		 mm_core_getid(core), core->nidle, core->nwork);
	for (int i = 0; i < MM_RUNQ_BINS; i++)
		mm_core_print_fiber_list(&core->runq.bins[i]);
	mm_core_print_fiber_list(&core->block);
}

void
mm_core_stats(void)
{
	mm_thread_t n = mm_core_getnum();
	for (mm_thread_t i = 0; i < n; i++) {
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
		mm_call_regular_start_hooks();
		mm_thread_local_summary(domain);
		mm_call_regular_thread_start_hooks();

		mm_thread_domain_barrier();
	} else {
		// Secondary cores have to wait until the primary core runs
		// the start hooks that initialize shared resources.
		mm_thread_domain_barrier();

		mm_timer_prepare(&core->time_manager, &space->xarena);
		mm_call_regular_thread_start_hooks();
	}
}

static void
mm_core_boot_term(struct mm_core *core)
{
	mm_thread_domain_barrier();

	// Call the stop hooks on the primary core.
	if (MM_CORE_IS_PRIMARY(core)) {
		mm_core_stats();
		mm_call_regular_stop_hooks();
	}

	mm_call_regular_thread_stop_hooks();
	mm_timer_cleanup(&core->time_manager);

	// TODO:
	//mm_fiber_destroy(core->master);
	//mm_fiber_destroy(core->dealer);
}

static void
mm_core_start_basic_tasks(struct mm_core *core)
{
	struct mm_fiber_attr attr;
	mm_fiber_attr_init(&attr);

	// Create the master fiber for this core and schedule it for execution.
	mm_fiber_attr_setpriority(&attr, MM_PRIO_MASTER);
	mm_fiber_attr_setname(&attr, "master");
	core->master = mm_fiber_create(&attr, mm_core_master, (mm_value_t) core);

	// Create the dealer fiber for this core and schedule it for execution.
	mm_fiber_attr_setpriority(&attr, MM_PRIO_DEALER);
	mm_fiber_attr_setname(&attr, "dealer");
	core->dealer = mm_fiber_create(&attr, mm_core_dealer, (mm_value_t) core);
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

	// Set pointer to the running fiber.
	core->fiber = core->boot;
	core->fiber->state = MM_FIBER_RUNNING;

#if ENABLE_TRACE
	mm_trace_context_prepare(&core->fiber->trace, "[%s][%d %s]",
				 mm_thread_getname(core->thread),
				 mm_fiber_getid(core->fiber),
				 mm_fiber_getname(core->fiber));
#endif

	// Initialize per-core resources.
	mm_core_boot_init(core);

	// Start master & dealer fibers.
	mm_core_start_basic_tasks(core);

	// Enable yielding to other fibers on busy waiting.
	mm_core_enable_yield(core);

	// Run the other fibers while there are any.
	core->state = MM_CORE_RUNNING;
	mm_fiber_yield();
	core->state = MM_CORE_INVALID;

	// Disable yielding to other fibers.
	mm_core_disable_yield(core);

	// Destroy per-core resources.
	mm_core_boot_term(core);

	// Invalidate the boot fiber.
	core->fiber->state = MM_FIBER_INVALID;
	core->fiber = NULL;

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

	// Create the core bootstrap fiber.
	struct mm_fiber_attr attr;
	mm_fiber_attr_init(&attr);
	mm_fiber_attr_setflags(&attr, MM_FIBER_BOOT | MM_FIBER_CANCEL_DISABLE);
	mm_fiber_attr_setpriority(&attr, MM_PRIO_BOOT);
	mm_fiber_attr_setname(&attr, "boot");
	core->boot = mm_fiber_create(&attr, mm_core_boot, (mm_value_t) core);

	LEAVE();
}

static void
mm_core_term_single(struct mm_core *core)
{
	ENTER();

	mm_wait_cache_cleanup(&core->wait_cache);

	mm_fiber_destroy(core->boot);

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
		return &core->fiber->trace;
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
	ASSERT(mm_regular_nthreads > 0);

	mm_fiber_init();
	mm_wait_init();
	mm_future_init();

#if ENABLE_TRACE
	mm_trace_set_getcontext(mm_core_gettracecontext);
#endif

	mm_core_set = mm_global_aligned_alloc(MM_CACHELINE, mm_regular_nthreads * sizeof(struct mm_core));
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
		mm_core_init_single(&mm_core_set[i]);

	LEAVE();
}

void
mm_core_term(void)
{
	ENTER();
	ASSERT(mm_regular_nthreads > 0);

	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
		mm_core_term_single(&mm_core_set[i]);
	mm_global_free(mm_core_set);

	mm_fiber_term();

	LEAVE();
}

void
mm_core_start(void)
{
	ENTER();
	ASSERT(mm_regular_nthreads > 0);

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
	ASSERT(mm_regular_nthreads > 0);

	// Set stop flag for core threads.
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++) {
		struct mm_core *core = &mm_core_set[i];
		mm_memory_store(core->stop, true);
		mm_thread_wakeup(core->thread);
	}

	LEAVE();
}
