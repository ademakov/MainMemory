/*
 * base/fiber/strand.c - MainMemory fiber strand.
 *
 * Copyright (C) 2013-2019  Aleksey Demakov
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
#include "base/fiber/fiber.h"
#include "base/memory/chunk.h"
#include "base/memory/global.h"
#include "base/memory/memory.h"
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

static void
mm_strand_idle(struct mm_strand *strand)
{
	ENTER();

	// Put the fiber into the wait queue.
	struct mm_fiber *fiber = strand->fiber;
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
	ASSERT(!mm_list_empty(&strand->idle));

	struct mm_link *link = mm_list_head(&strand->idle);
	struct mm_fiber *fiber = containerof(link, struct mm_fiber, wait_queue);

	// Get a fiber from the wait queue.
	ASSERT((fiber->flags & MM_FIBER_WAITING) != 0);
	mm_list_delete(&fiber->wait_queue);
	fiber->flags &= ~MM_FIBER_WAITING;
	strand->nidle--;

	// Put the fiber to the run queue.
	mm_fiber_run(fiber);

	LEAVE();
}

/**********************************************************************
 * Fiber queue.
 **********************************************************************/

#if ENABLE_SMP
static void
mm_strand_run_fiber_req(struct mm_event_listener *listener UNUSED, uintptr_t *arguments)
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
		struct mm_event_listener *listener = fiber->strand->listener;
		mm_event_call_1(listener, mm_strand_run_fiber_req, (uintptr_t) fiber);
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
	struct mm_event_task_slot *slot = (struct mm_event_task_slot *) arg;

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
	struct mm_event_task_slot slot;
	slot.task = NULL;

	// Ensure cleanup on exit.
	mm_fiber_cleanup_push(mm_strand_worker_cleanup, &slot);

	// Run in a loop forever getting and executing tasks.
	struct mm_strand *const strand = (struct mm_strand *) arg;
	for (;;) {
		// Try to get a task.
		if (!mm_event_task_list_get(&strand->listener->tasks, &slot)) {
			// Wait for a task standing at the front of the idle queue.
			mm_strand_idle(strand);
			continue;
		}

		// Execute the task.
		const struct mm_event_task *const task = slot.task;
		const mm_value_t result = (task->execute)(slot.task_arg);
		// Protect against a spurious cancel call from the completion routine.
		mm_memory_store(slot.task, NULL);
		// Perform completion notification on return.
		(task->complete)(slot.task_arg, result);
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

	// Halt the strand waiting for incoming events.
	mm_event_listen(strand->listener, MM_STRAND_HALT_TIMEOUT);

	// Indicate that clocks need to be updated.
	mm_timer_resetclocks(&strand->time_manager);

	LEAVE();
}

static mm_value_t
mm_strand_master(mm_value_t arg)
{
	ENTER();

	struct mm_strand *strand = (struct mm_strand *) arg;

	// Run until stopped by a user request.
	while (!mm_memory_load(strand->stop)) {
		// Check to see if there are pending tasks.
		if (mm_event_task_list_empty(&strand->listener->tasks)) {
			// Release excessive resources allocated by fibers.
			mm_strand_trim(strand);
			// Halt waiting for any incoming events.
			mm_strand_halt(strand);
		}

		// Activate a worker fiber to handle pending tasks.
		if (strand->nidle) {
			// Activate an idle worker.
			mm_strand_poke(strand);
		} else {
			// Report the status of all fibers.
			if (mm_get_verbose_enabled())
				mm_strand_print_fibers(strand);
			// Create a new worker if feasible.
			if (!mm_event_task_list_empty(&strand->listener->tasks)
			    && strand->nworkers < strand->nworkers_max)
				mm_strand_worker_create(strand);
		}

		// Run active fibers if any.
		mm_fiber_yield();
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
		 (unsigned long) mm_event_task_list_size(&strand->listener->tasks));
	for (int i = 0; i < MM_RUNQ_BINS; i++)
		mm_strand_print_fiber_list(&strand->runq.bins[i]);
	mm_strand_print_fiber_list(&strand->block);
}

void NONNULL(1)
mm_strand_stats(struct mm_strand *strand)
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
mm_strand_stop_req(struct mm_event_listener *listener, uintptr_t *arguments UNUSED)
{
	ENTER();

	listener->strand->stop = true;

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

	strand->state = MM_STRAND_INVALID;

	strand->nidle = 0;
	strand->nworkers = 0;
	strand->nworkers_min = MM_NWORKERS_MIN;
	strand->nworkers_max = MM_NWORKERS_MAX;
	strand->cswitch_count = 0;

	strand->master = NULL;
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

	// Destroy the cache of wait-set entries.
	mm_wait_cache_cleanup(&strand->wait_cache);

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
	// Destroy the boot fiber.
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

	// Force creation of the minimal number of worker fibers.
	while (strand->nworkers < strand->nworkers_min)
		mm_strand_worker_create(strand);

	// Relinquish control to the created fibers. Once these fibers and
	// any fibers created later exit the control returns here.
	strand->state = MM_STRAND_RUNNING;
	mm_fiber_yield();
	strand->state = MM_STRAND_INVALID;
}

void NONNULL(1)
mm_strand_stop(struct mm_strand *strand)
{
	ENTER();

	mm_event_call_0(strand->listener, mm_strand_stop_req);

	LEAVE();
}
