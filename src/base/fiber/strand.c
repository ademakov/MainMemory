/*
 * base/fiber/strand.c - MainMemory fiber strand.
 *
 * Copyright (C) 2013-2020  Aleksey Demakov
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

#include "base/async.h"
#include "base/bitset.h"
#include "base/exit.h"
#include "base/logger.h"
#include "base/settings.h"
#include "base/fiber/fiber.h"
#include "base/thread/local.h"
#include "base/thread/thread.h"
#include "base/util/hook.h"

#include "net/net.h"

#include <stdio.h>

#define MM_NWORKERS_MIN		2
#define MM_NWORKERS_MAX		256

/**********************************************************************
 * Idle queue.
 **********************************************************************/

static void
mm_strand_idle(struct mm_strand *const strand, struct mm_context *const context, struct mm_fiber *const fiber)
{
	ENTER();

	// Put the fiber into the idle queue.
	mm_list_insert(&strand->idle, &fiber->wait_queue);
	ASSERT((fiber->flags & MM_FIBER_WAITING) == 0);
	fiber->flags |= MM_FIBER_WAITING;
	strand->nidle++;

	// Wait until poked.
	mm_fiber_block(context);

	// Remove the fiber from the idle queue.
	mm_list_delete(&fiber->wait_queue);
	fiber->flags &= ~MM_FIBER_WAITING;
	strand->nidle--;

	LEAVE();
}

static void
mm_strand_poke(struct mm_strand *strand)
{
	ENTER();
	ASSERT(!mm_list_empty(&strand->idle));

	// Get a fiber from the idle queue.
	struct mm_link *link = mm_list_head(&strand->idle);
	struct mm_fiber *fiber = containerof(link, struct mm_fiber, wait_queue);

	// Put the fiber to the run queue.
	mm_fiber_run(fiber);

	LEAVE();
}

/**********************************************************************
 * Fiber queue.
 **********************************************************************/

#if ENABLE_SMP
static void
mm_strand_run_fiber_req(struct mm_context *context UNUSED, uintptr_t *arguments)
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
	if (fiber->strand == mm_context_strand()) {
		// Put the fiber to the run queue directly.
		mm_fiber_run(fiber);
	} else {
		// Submit the fiber to the thread request queue.
		struct mm_context *context = fiber->strand->context;
		mm_async_call_1(context, mm_strand_run_fiber_req, (uintptr_t) fiber);
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
	struct mm_strand *strand = mm_context_strand();
	struct mm_task_slot *slot = (struct mm_task_slot *) arg;

	// Notify that the current work has been canceled.
	if (slot->task != NULL)
		(slot->task->complete)(slot->task_arg, MM_RESULT_CANCELED);

	// Account for the exiting worker.
	strand->nworkers--;
}

static mm_value_t
mm_strand_worker(mm_value_t arg)
{
	ENTER();

	// The task to execute and possibly cancel.
	struct mm_task_slot slot;
	slot.task = NULL;

	// Ensure cleanup on exit.
	mm_fiber_cleanup_push(mm_strand_worker_cleanup, &slot);

	// Run in a loop forever getting and executing tasks.
	struct mm_strand *const strand = (struct mm_strand *) arg;
	struct mm_context *const context = strand->context;
	struct mm_fiber *const fiber = context->fiber;
	for (;;) {
		// Try to get a task.
		if (!mm_task_list_get(&context->tasks, &slot)) {
			// Wait for a task standing at the front of the idle queue.
			mm_strand_idle(strand, context, fiber);
			continue;
		}

		// Execute the task.
		const struct mm_task *const task = slot.task;
		const mm_value_t result = (task->execute)(slot.task_arg);
		// Protect against a spurious cancel call from the completion routine.
		mm_memory_store(slot.task, NULL);
		// Perform completion notification on return.
		(task->complete)(slot.task_arg, result);

		// Reset the priority that could have been temporary raised.
		mm_fiber_restore_priority(fiber);

		// Handle any incoming async calls.
		mm_async_handle_calls(context);
	}

	// Cleanup on return.
	mm_fiber_cleanup_pop(true);

	LEAVE();
	return 0;
}

static void
mm_strand_worker_create(struct mm_strand *strand)
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
	mm_fiber_create(&attr, mm_strand_worker, (mm_value_t) strand);

	// Account for the newcomer worker.
	strand->nworkers++;

	LEAVE();
}

/**********************************************************************
 * Master fiber.
 **********************************************************************/

// Master loop sleep time - 10 seconds
#define MM_STRAND_HALT_TIMEOUT	((mm_timeout_t) 10 * 1000 * 1000)

static mm_value_t
mm_strand_master(mm_value_t arg)
{
	ENTER();

	struct mm_context *const context = (struct mm_context *) arg;
	struct mm_strand *const strand = context->strand;

	uint32_t spin_limit = mm_settings_get_uint32("event-poll-spin-limit", 4);
	uint32_t spin_count = 0;

	// Run until stopped by a user request.
	for (;;) {
		// Run active fibers if any.
		mm_fiber_yield(context);

		// Check for stop signal.
		if (unlikely(strand->stop))
			break;

		// Check for available tasks.
		if (mm_task_list_empty(&context->tasks)) {
			// Cleanup the temporary data.
			mm_wait_cache_truncate(&strand->wait_cache);
			// Collect released context memory.
			mm_memory_cache_collect(&context->cache);

			// Check for I/O events and timers.
			const bool spin = spin_count < spin_limit || context->tasks_request_in_progress;
			if (mm_event_poll(context, spin ? 0 : MM_STRAND_HALT_TIMEOUT)) {
				spin_count = 0;
			} else {
				spin_count++;
				// Request tasks from a peer thread.
				mm_context_request_tasks(context);
			}

			// If there are too many tasks now then share them with peers.
			mm_context_distribute_tasks(context);
			// Yield.
			continue;
		}

		// Check for idle worker fibers to handle available tasks.
		if (strand->nidle) {
			// Wake an idle worker fiber.
			mm_strand_poke(strand);
			// Yield.
			continue;
		}

		// Check for I/O events and timers which might activate some
		// blocked worker fibers.
		mm_event_poll(context, 0);
		// Run active fibers if any.
		mm_fiber_yield(context);

		// Check to see if any worker fibers indeed made progress and
		// are idle now.
		if (strand->nidle)
			continue;

		// Report the status of all fibers.
		if (mm_get_verbose_enabled())
			mm_strand_print_fibers(strand);

		// Add a new worker if feasible.
		if (strand->nworkers < strand->nworkers_max) {
			mm_strand_worker_create(strand);
		} else {
			mm_warning(0, "all allowed worker fibers are busy");
		}
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
	mm_brief("fibers on thread %d (#idle=%u, #task=%lu):",
		 mm_thread_getnumber(strand->thread), strand->nidle,
		 (unsigned long) mm_task_list_size(&strand->context->tasks));
	for (int i = 0; i < MM_RUNQ_BINS; i++)
		mm_strand_print_fiber_list(&strand->runq.bins[i]);
	mm_strand_print_fiber_list(&strand->block);
}

void NONNULL(1)
mm_strand_report_stats(struct mm_strand *strand)
{
	mm_verbose("thread %d: cswitches=%llu, workers=%lu",
		   mm_thread_getnumber(strand->thread),
		   (unsigned long long) strand->cswitch_count,
		   (unsigned long) strand->nworkers);
}

/**********************************************************************
 * Strand initialization and termination.
 **********************************************************************/

static void
mm_strand_stop_req(struct mm_context *context, uintptr_t *arguments UNUSED)
{
	ENTER();

	context->strand->stop = true;

	LEAVE();
}

void NONNULL(1)
mm_strand_prepare(struct mm_strand *strand)
{
	ENTER();

	mm_runq_prepare(&strand->runq);
	mm_list_prepare(&strand->idle);
	mm_list_prepare(&strand->dead);
	mm_list_prepare(&strand->block);
	mm_list_prepare(&strand->async);

	mm_wait_cache_prepare(&strand->wait_cache);

	strand->nidle = 0;
	strand->nworkers = 0;
	strand->nworkers_min = MM_NWORKERS_MIN;
	strand->nworkers_max = MM_NWORKERS_MAX;
	strand->cswitch_count = 0;

	strand->master = NULL;
	strand->thread = NULL;

	strand->stop = false;

	// Create the strand bootstrap fiber.
	strand->boot = mm_fiber_create_boot(strand);

	LEAVE();
}

void NONNULL(1)
mm_strand_cleanup(struct mm_strand *strand)
{
	ENTER();

	// Destroy the cache of wait-set entries.
	mm_wait_cache_cleanup(&strand->wait_cache);

	// Destroy the boot fiber.
	mm_fiber_destroy(strand->boot);

	// Flush logs before memory space with possible log chunks is unmapped.
	mm_log_relay();
	mm_log_flush();

	LEAVE();
}

void NONNULL(1, 2)
mm_strand_loop(struct mm_strand *const strand, struct mm_context *const context)
{
	struct mm_fiber_attr attr;

	// Create a master fiber and schedule it for execution.
	mm_fiber_attr_init(&attr);
	mm_fiber_attr_setpriority(&attr, MM_PRIO_MASTER);
	mm_fiber_attr_setname(&attr, "master");
	strand->master = mm_fiber_create(&attr, mm_strand_master, (mm_value_t) context);

	// Force creation of the minimal number of worker fibers.
	while (strand->nworkers < strand->nworkers_min)
		mm_strand_worker_create(strand);

	// Relinquish control to the created fibers. Once these fibers and
	// any fibers created later exit the control returns here.
	context->status = MM_CONTEXT_RUNNING;
	mm_fiber_yield(context);
	context->status = MM_CONTEXT_PENDING;

	// Destroy all the blocked fibers.
	while (!mm_list_empty(&strand->block)) {
		struct mm_link *link = mm_list_remove_head(&strand->block);
		struct mm_fiber *fiber = containerof(link, struct mm_fiber, queue);
		DEBUG("blocked fiber: %s", mm_fiber_getname(fiber));
		mm_fiber_destroy(fiber);

	}
	// Destroy all the dead fibers.
	while (!mm_list_empty(&strand->dead)) {
		struct mm_link *link = mm_list_remove_head(&strand->dead);
		struct mm_fiber *fiber = containerof(link, struct mm_fiber, queue);
		DEBUG("dead fiber: %s", mm_fiber_getname(fiber));
		mm_fiber_destroy(fiber);
	}
}

void NONNULL(1)
mm_strand_stop(struct mm_strand *const strand)
{
	ENTER();

	mm_async_call_0(strand->context, mm_strand_stop_req);

	LEAVE();
}
