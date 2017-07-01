/*
 * base/fiber/strand.c - MainMemory fiber strand.
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

#include "base/fiber/strand.h"

#include "base/bitset.h"
#include "base/exit.h"
#include "base/logger.h"
#include "base/event/dispatch.h"
#include "base/fiber/fiber.h"
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

// A strand associated with the running thread.
__thread struct mm_strand *__mm_strand_self;

/**********************************************************************
 * Idle queue.
 **********************************************************************/

void
mm_strand_idle(struct mm_strand *strand, bool tail)
{
	ENTER();

	// Put the fiber into the wait queue.
	struct mm_fiber *fiber = strand->fiber;
	if (tail)
		mm_list_append(&strand->idle, &fiber->wait_queue);
	else
		mm_list_insert(&strand->idle, &fiber->wait_queue);

	ASSERT((fiber->flags & MM_FIBER_WAITING) == 0);
	fiber->flags |= MM_FIBER_WAITING;
	strand->nidle++;

	// Wait until poked.
	mm_fiber_block();

	// Normally an idle fiber starts after being poked and
	// in this case it should already be removed from the
	// wait list. But if the fiber has started for another
	// reason it must be removed from the wait list here.
	if (unlikely((fiber->flags & MM_FIBER_WAITING) != 0)) {
		mm_list_delete(&fiber->wait_queue);
		fiber->flags &= ~MM_FIBER_WAITING;
		strand->nidle--;
	}

	LEAVE();
}

static void
mm_strand_poke(struct mm_strand *strand)
{
	ENTER();

	if (likely(!mm_list_empty(&strand->idle))) {
		struct mm_link *link = mm_list_head(&strand->idle);
		struct mm_fiber *fiber = containerof(link, struct mm_fiber, wait_queue);

		// Get a fiber from the wait queue.
		ASSERT((fiber->flags & MM_FIBER_WAITING) != 0);
		mm_list_delete(&fiber->wait_queue);
		fiber->flags &= ~MM_FIBER_WAITING;
		strand->nidle--;

		// Put the fiber to the run queue.
		mm_fiber_run(fiber);
	}

	LEAVE();
}

/**********************************************************************
 * Work queue.
 **********************************************************************/

static inline bool
mm_strand_has_work(struct mm_strand *strand)
{
	return strand->nwork != 0;
}

static struct mm_work *
mm_strand_get_work(struct mm_strand *strand)
{
	ASSERT(mm_strand_has_work(strand));

	strand->nwork--;
	struct mm_qlink *link = mm_queue_remove(&strand->workq);
	return containerof(link, struct mm_work, link);
}

static void
mm_strand_add_work(struct mm_strand *strand, struct mm_work *work)
{
	// Enqueue the work item.
	mm_queue_append(&strand->workq, &work->link);
	strand->nwork++;

	// If there is a fiber waiting for work then let it run now.
	mm_strand_poke(strand);
}

#if ENABLE_SMP

static void
mm_strand_post_work_req(uintptr_t *arguments)
{
	ENTER();

	struct mm_work *work = (struct mm_work *) arguments[0];
	mm_strand_add_work(mm_strand_selfptr(), work);

	LEAVE();
}

void NONNULL(2)
mm_strand_post_work(mm_thread_t target, struct mm_work *work)
{
	ENTER();

	// Dispatch the work item.
	if (target == MM_THREAD_NONE) {
		// Submit the work item to the domain request queue.
		struct mm_domain *domain = mm_domain_selfptr();
		mm_domain_post_1(domain, mm_strand_post_work_req, (uintptr_t) work);
		mm_domain_notify(domain);
	} else {
		struct mm_strand *self = mm_strand_selfptr();
		ASSERT(target == MM_THREAD_SELF || target < mm_regular_nthreads);
		struct mm_strand *dest = target == MM_THREAD_SELF ? self: &mm_regular_strands[target];
		if (dest == self) {
			// Enqueue it directly if on the same strand.
			mm_strand_add_work(dest, work);
		} else {
			// Submit it to the thread request queue.
			struct mm_thread *thread = dest->thread;
			mm_thread_post_1(thread, mm_strand_post_work_req, (uintptr_t) work);
		}
	}

	LEAVE();
}

#else

void
mm_strand_post_work(mm_thread_t target, struct mm_work *work)
{
	ENTER();

	(void) target;
	mm_strand_add_work(mm_strand_selfptr(), work);

	LEAVE();
}

#endif

/**********************************************************************
 * Fiber queue.
 **********************************************************************/

#if ENABLE_SMP
static void
mm_strand_run_fiber_req(uintptr_t *arguments)
{
	ENTER();

	struct mm_fiber *fiber = (struct mm_fiber *) arguments[0];
	mm_fiber_run(fiber);

	LEAVE();
}
#endif

void NONNULL(1)
mm_strand_run_fiber(struct mm_fiber *fiber)
{
	ENTER();

#if ENABLE_SMP
	if (fiber->strand == mm_strand_selfptr()) {
		// Put the fiber to the run queue directly.
		mm_fiber_run(fiber);
	} else {
		// Submit the fiber to the thread request queue.
		struct mm_thread *thread = fiber->strand->thread;
		mm_thread_post_1(thread, mm_strand_run_fiber_req, (uintptr_t) fiber);
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
mm_strand_worker_cleanup(uintptr_t arg)
{
	struct mm_strand *strand = mm_strand_selfptr();
	struct mm_work *work = *((struct mm_work **) arg);

	// Notify that the current work has been canceled.
	if (work != NULL)
		(work->vtable->complete)(work, MM_RESULT_CANCELED);

	// Wake up the master possibly waiting for worker availability.
	if (strand->nworkers == strand->nworkers_max)
		mm_fiber_run(strand->master);

	// Account for the exiting worker.
	strand->nworkers--;
}

static mm_value_t
mm_strand_worker(mm_value_t arg)
{
	ENTER();

	// The work item to cancel.
	struct mm_work *cancel = NULL;

	// Ensure cleanup on exit.
	mm_fiber_cleanup_push(mm_strand_worker_cleanup, &cancel);

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

	struct mm_strand *const strand = mm_strand_selfptr();
	for (;;) {
		// Wait for work standing at the front of the idle queue.
		while (!mm_strand_has_work(strand))
			mm_strand_idle(strand, false);

		// Handle the first available work item.
		struct mm_work *work = mm_strand_get_work(strand);
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
mm_strand_worker_create(struct mm_strand *strand, mm_value_t arg)
{
	ENTER();

	// Make a unique worker name.
	char name[MM_FIBER_NAME_SIZE];
	snprintf(name, MM_FIBER_NAME_SIZE, "worker %u", strand->nworkers);

	// Make a new worker fiber and start it.
	struct mm_fiber_attr attr;
	mm_fiber_attr_init(&attr);
	mm_fiber_attr_setpriority(&attr, MM_PRIO_WORKER);
	mm_fiber_attr_setname(&attr, name);
	mm_fiber_create(&attr, mm_strand_worker, arg);

	// Account for the newcomer worker.
	strand->nworkers++;

	LEAVE();
}

/**********************************************************************
 * Master fiber.
 **********************************************************************/

static mm_value_t
mm_strand_master(mm_value_t arg)
{
	ENTER();

	struct mm_strand *strand = (struct mm_strand *) arg;
	bool verbose = mm_get_verbose_enabled();

	// Force creation of the minimal number of workers.
	while (strand->nworkers < strand->nworkers_min)
		mm_strand_worker_create(strand, 0);

	while (!mm_memory_load(strand->stop)) {
		// Check to see if there are enough workers.
		if (strand->nworkers >= strand->nworkers_max) {
			mm_fiber_block();
			continue;
		}

		// Wait for work at the back end of the idle queue.
		// So any idle worker would take work before the master.
		mm_strand_idle(strand, true);

		// Check to see if there is outstanding work.
		if (mm_strand_has_work(strand)) {
			// Take the first available work item.
			struct mm_work *work = mm_strand_get_work(strand);

			// Make a new worker fiber to handle it.
			mm_strand_worker_create(strand, (mm_value_t) work);

			// Inform about the status of all fibers.
			if (verbose)
				mm_strand_print_fibers(strand);
		}
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Dealer fiber.
 **********************************************************************/

// Dealer loop sleep time - 10 seconds
#define MM_STRAND_HALT_TIMEOUT	((mm_timeout_t) 10 * 1000 * 1000)

void NONNULL(1)
mm_strand_execute_requests(struct mm_strand *strand)
{
	ENTER();

	struct mm_thread *thread = strand->thread;

	// Execute requests.
	struct mm_request_data request;
	if (mm_thread_receive(thread, &request)) {
		// Enter the state that forbids a recursive fiber switch.
		mm_strand_state_t state = strand->state;
		strand->state = MM_STRAND_CSWITCH;

		do {
			mm_request_execute(&request);
			strand->thread_request_count++;
		} while (mm_thread_receive(thread, &request));

		// Restore normal running state.
		strand->state = state;
	}

	LEAVE();
}

static bool NONNULL(1)
mm_strand_pull_domain_request(struct mm_strand *strand UNUSED)
{
	ENTER();
	bool rc = false;

#if ENABLE_SMP
	struct mm_domain *domain = mm_thread_getdomain(strand->thread);

	struct mm_request_data request;
	if (mm_domain_receive(domain, &request)) {
		mm_request_execute(&request);
		strand->domain_request_count++;
		rc = true;
	}
#endif

	LEAVE();
	return rc;
}

static void
mm_strand_trim(struct mm_strand *strand)
{
	ENTER();

	// Cleanup the temporary data.
	mm_wait_cache_truncate(&strand->wait_cache);
	mm_chunk_enqueue_deferred(strand->thread, true);

#if ENABLE_SMP
	// Trim private memory space.
	mm_private_space_trim(mm_thread_getspace(strand->thread));
#endif

	LEAVE();
}

static void
mm_strand_halt(struct mm_strand *strand)
{
	ENTER();

	// Count it.
	strand->halt_count++;

	// Get the closest expiring timer if any.
	mm_timeval_t wake_time = mm_timer_next(&strand->time_manager);
	if (wake_time != MM_TIMEVAL_MAX) {
		// Calculate the timeout.
		mm_timeout_t timeout = MM_STRAND_HALT_TIMEOUT;
		mm_timeval_t time = mm_strand_gettime(strand);
		if (wake_time < (time + timeout)) {
			if (wake_time > time)
				timeout = wake_time - time;
			else
				timeout = 0;
		}

		// Halt the strand waiting for incoming events.
		mm_event_listen(mm_thread_getlistener(strand->thread), timeout);

		// Indicate that clocks need to be updated.
		mm_timer_resetclocks(&strand->time_manager);

		// Fire reached timers.
		mm_timer_tick(&strand->time_manager);

	} else {
		// Halt the strand waiting for incoming events.
		mm_event_listen(mm_thread_getlistener(strand->thread), MM_STRAND_HALT_TIMEOUT);

		// Indicate that clocks need to be updated.
		mm_timer_resetclocks(&strand->time_manager);
	}

	LEAVE();
}

static mm_value_t
mm_strand_dealer(mm_value_t arg)
{
	ENTER();

	struct mm_strand *strand = (struct mm_strand *) arg;

	while (!mm_memory_load(strand->stop)) {
		// Run the queued fibers if any.
		do {
			mm_fiber_yield();
		} while (mm_strand_pull_domain_request(strand));

		// Release excessive resources allocated by fibers.
		mm_strand_trim(strand);

		// Halt waiting for incoming requests.
		mm_strand_halt(strand);
	}

	LEAVE();
	return 0;
}

/**********************************************************************
 * Strand diagnostics and statistics.
 **********************************************************************/

static void
mm_strand_print_fiber_list(struct mm_list *list)
{
	struct mm_link *link = &list->base;
	while (!mm_list_is_tail(list, link)) {
		link = link->next;
		struct mm_fiber *fiber = containerof(link, struct mm_fiber, queue);
		mm_fiber_print_status(fiber);
	}
}

void NONNULL(1)
mm_strand_print_fibers(struct mm_strand *strand)
{
	mm_brief("fibers on thread %d (#idle=%u, #work=%u):",
		 mm_thread_getnumber(strand->thread), strand->nidle, strand->nwork);
	for (int i = 0; i < MM_RUNQ_BINS; i++)
		mm_strand_print_fiber_list(&strand->runq.bins[i]);
	mm_strand_print_fiber_list(&strand->block);
}

void
mm_strand_stats(void)
{
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++) {
		struct mm_strand *strand = &mm_regular_strands[i];
		mm_verbose("strand %d: cycles=%llu, cswitches=%llu, requests=%llu/%llu, workers=%lu",
			   i, (unsigned long long) strand->halt_count,
			   (unsigned long long) strand->cswitch_count,
			   (unsigned long long) strand->thread_request_count,
			   (unsigned long long) strand->domain_request_count,
			   (unsigned long) strand->nworkers);
	}
}

/**********************************************************************
 * Strand initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_strand_prepare(struct mm_strand *strand)
{
	ENTER();

	mm_runq_prepare(&strand->runq);
	mm_list_prepare(&strand->idle);
	mm_list_prepare(&strand->dead);
	mm_list_prepare(&strand->block);
	mm_list_prepare(&strand->async);
	mm_queue_prepare(&strand->workq);

	mm_wait_cache_prepare(&strand->wait_cache);

	strand->state = MM_STRAND_INVALID;

	strand->nwork = 0;
	strand->nidle = 0;
	strand->nworkers = 0;
	strand->nworkers_min = MM_NWORKERS_MIN;
	strand->nworkers_max = MM_NWORKERS_MAX;

	strand->halt_count = 0;
	strand->cswitch_count = 0;
	strand->thread_request_count = 0;
	strand->domain_request_count = 0;

	strand->master = NULL;
	strand->dealer = NULL;

	strand->thread = NULL;

	strand->stop = false;

	// Create the strand bootstrap fiber.
	strand->boot = mm_fiber_create_boot();

	LEAVE();
}

void NONNULL(1)
mm_strand_cleanup(struct mm_strand *strand)
{
	ENTER();

	mm_wait_cache_cleanup(&strand->wait_cache);

	// TODO:
	//mm_fiber_destroy(strand->master);
	//mm_fiber_destroy(strand->dealer);
	mm_fiber_destroy(strand->boot);

	// Flush logs before memory space with possible log chunks is unmapped.
	mm_log_relay();
	mm_log_flush();

	LEAVE();
}

void NONNULL(1)
mm_strand_start(struct mm_strand *strand)
{
	struct mm_fiber_attr attr;

	// Create a master fiber and schedule it for execution.
	mm_fiber_attr_init(&attr);
	mm_fiber_attr_setpriority(&attr, MM_PRIO_MASTER);
	mm_fiber_attr_setname(&attr, "master");
	strand->master = mm_fiber_create(&attr, mm_strand_master, (mm_value_t) strand);

	// Create a dealer fiber and schedule it for execution.
	mm_fiber_attr_init(&attr);
	mm_fiber_attr_setpriority(&attr, MM_PRIO_DEALER);
	mm_fiber_attr_setname(&attr, "dealer");
	strand->dealer = mm_fiber_create(&attr, mm_strand_dealer, (mm_value_t) strand);

	// Relinquish control to the created fibers. Once these fibers and
	// any worker fibers (created by the master) exit then control will
	// return here.
	strand->state = MM_STRAND_RUNNING;
	mm_fiber_yield();
	strand->state = MM_STRAND_INVALID;
}

void NONNULL(1)
mm_strand_stop(struct mm_strand *strand)
{
	mm_memory_store(strand->stop, true);
	mm_thread_wakeup(strand->thread);
}
