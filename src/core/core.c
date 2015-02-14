/*
 * core/core.c - MainMemory core.
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

#include "core/core.h"
#include "core/future.h"
#include "core/port.h"
#include "core/task.h"
#include "core/work.h"

#include "event/dispatch.h"

#include "base/bitset.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/mem/cdata.h"
#include "base/mem/chunk.h"
#include "base/mem/mem.h"
#include "base/thr/domain.h"
#include "base/thr/thread.h"
#include "base/util/exit.h"
#include "base/util/hook.h"

#include "net/net.h"

#include <stdio.h>
#include <unistd.h>

#ifdef HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif

#define MM_DEFAULT_CORES	1
#define MM_DEFAULT_WORKERS	256

#if ENABLE_SMP
# define MM_CORE_IS_PRIMARY(core)	(core == mm_core_set)
#else
# define MM_CORE_IS_PRIMARY(core)	(true)
#endif

// The core set.
mm_core_t mm_core_num;
struct mm_core *mm_core_set;
struct mm_domain mm_core_domain;

// A core associated with the running thread.
__thread struct mm_core *mm_core;

// The set of cores with event loops.
static struct mm_bitset mm_core_event_affinity;

// Common event dispatch.
static struct mm_dispatch mm_core_dispatch;

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
mm_core_post_work(mm_core_t core_id, struct mm_work *work)
{
	ENTER();

#if ENABLE_SMP
	// Get the target core.
	struct mm_core *core;
	if (core_id != MM_CORE_NONE) {
		core = mm_core_getptr(core_id);
	} else {
		core = mm_core;
	}

	// Dispatch the work item.
	if (core == mm_core) {
		// Enqueue it directly if on the same core.
		mm_core_add_work(core, work);
	} else {
		// Put the item to the target core inbox.
		for (;;) {
			bool ok = mm_ring_spsc_locked_put(&core->inbox, work);

			// Wakeup the target core if it is asleep.
			mm_listener_notify(&core->listener, &mm_core_dispatch);

			if (ok) {
				break;
			} else {
				// TODO: backoff
				mm_task_yield();
			}
		}
	}
#else
	(void) core_id;
	mm_core_add_work(mm_core, work);
#endif

	LEAVE();
}

void
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

#if ENABLE_SMP
static void
mm_core_receive_work(struct mm_core *core)
{
	ENTER();

	struct mm_work *work;
	while (mm_ring_spsc_get(&core->inbox, (void **) &work))
		mm_core_add_work(core, work);

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

#if ENABLE_SMP
	if (task->core == mm_core) {
		// Put the task to the core run queue directly.
		mm_task_run(task);
	} else {
		// Put the task to the target core sched ring.
		for (;;) {
			bool ok = mm_ring_spsc_locked_put(&task->core->sched, task);

			// Wakeup the target core if it is asleep.
			mm_listener_notify(&task->core->listener, &mm_core_dispatch);

			if (ok) {
				break;
			} else {
				// TODO: backoff
				mm_task_yield();
			}
		}
	}
#else
	mm_task_run(task);
#endif

	LEAVE();
}

#if ENABLE_SMP
static void
mm_core_receive_tasks(struct mm_core *core)
{
	ENTER();

	struct mm_task *task;
	while (mm_ring_spsc_get(&core->sched, (void **) &task))
		mm_task_run(task);

	LEAVE();
}
#else
# define mm_core_receive_tasks(core) ((void) core)
#endif

/**********************************************************************
 * Chunk allocation and reclamation.
 **********************************************************************/

mm_chunk_t
mm_core_chunk_select(void)
{
	mm_chunk_t tag = mm_core_selfid();
	if (tag == MM_CORE_NONE) {
		// Common arena could only be used after it gets
		// initialized during bootstrap.
		if (likely(mm_common_space_is_ready()))
			tag = MM_CHUNK_COMMON;
		else
			tag = MM_CHUNK_GLOBAL;
	}
	return tag;
}

void *
mm_core_chunk_alloc(mm_chunk_t tag __attribute__((unused)), size_t size)
{
	ASSERT(tag == mm_core_selfid());
	return mm_local_alloc(size);
}

void
mm_core_chunk_free(mm_chunk_t tag, void *chunk)
{
	mm_core_t core = mm_core_selfid();
	if (core == tag) {
		mm_local_free(chunk);
	} else {
		ASSERT(!MM_CHUNK_IS_ARENA_TAG(tag));

		// Put the chunk to the target core chunks ring.
		struct mm_core *core = mm_core_getptr(tag);
		for (;;) {
			bool ok = mm_ring_spsc_locked_put(&core->chunks, chunk);

			// Actual reclamation may wait a little bit so
			// don't wakeup the core unless the ring is full.
			if (ok) {
				break;
			} else if (unlikely(mm_memory_load(core->stop))) {
#if 0
				mm_warning(0, "lost a chunk as core %d is stopped", tag);
#endif
				break;
			}

			// Wakeup the target core if it is asleep.
			mm_listener_notify(&core->listener, &mm_core_dispatch);
		}
	}
}

static void
mm_core_destroy_chunks(struct mm_core *core)
{
	ENTER();

	void *chunk;
	while (mm_ring_spsc_get(&core->chunks, &chunk)) {
		ASSERT(mm_chunk_gettag((struct mm_chunk *) chunk) == mm_core_selfid());
		mm_local_free(chunk);
	}

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

	if (work->complete == NULL) {
		// Destroy unneeded work data.
		mm_work_destroy(work);
		// Execute the work routine.
		routine(value);
	} else {
		// Ensure completion notification on task cancellation.
		mm_task_cleanup_push(mm_core_worker_cancel, work);

		// Execute the work routine.
		value = routine(value);
		// Perform completion notification on return.
		work->complete(work, value);

		mm_task_cleanup_pop(false);
	}

	LEAVE();
}

static void
mm_core_worker_cleanup(uintptr_t arg __attribute__((unused)))
{
	// Wake up the master possibly waiting for worker availability.
	if (mm_core->nworkers == mm_core->nworkers_max)
		mm_task_run(mm_core->master);

	// Account for the exiting worker.
	mm_core->nworkers--;
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
	struct mm_core *core = mm_core;

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
#define MM_DEALER_HALT_TIMEOUT	((mm_timeout_t) 10 * 1000 * 1000)

// Dealer loop hang on times
#define MM_DEALER_POLL_TIMEOUT	((mm_timeout_t) 10)
#define MM_DEALER_HOLD_TIMEOUT	((mm_timeout_t) 25)

static mm_atomic_uint32_t mm_core_deal_count;

static void
mm_core_deal(struct mm_core *core)
{
	ENTER();

	// Start current timer tasks.
	mm_timer_tick(&core->time_manager);

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

	// Get the closest timer expiration time.
	mm_timeval_t next_timer = mm_timer_next(&core->time_manager);

	// Get the halt timeout.
	mm_timeout_t timeout = MM_DEALER_HALT_TIMEOUT;
	if (unlikely(next_timer < core->time_manager.time))
		timeout = 0;
	else if (next_timer < core->time_manager.time + MM_DEALER_HALT_TIMEOUT)
		timeout = next_timer - core->time_manager.time;
	else
		timeout = MM_DEALER_HALT_TIMEOUT;

	mm_listener_listen(&core->listener, &mm_core_dispatch, timeout);

	mm_timer_update_time(&core->time_manager);
	mm_timer_update_real_time(&core->time_manager);

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
	//uint32_t deal = mm_memory_load(mm_core_deal_count);
	//mm_verbose("core stats: deal = %u", deal);
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
 * Shared Core Space Initialization and Termination.
 **********************************************************************/

#if ENABLE_SMP
struct mm_common_space mm_shared_space;
mm_chunk_t mm_shared_chunk_tag;
#endif

static void
mm_shared_space_init(void)
{
#if ENABLE_SMP
	mm_common_space_prepare(&mm_shared_space, true);
	mm_shared_chunk_tag = mm_chunk_add_arena(&mm_shared_space.arena);
#endif
}

static void
mm_shared_space_term(void)
{
#if ENABLE_SMP
	mm_common_space_cleanup(&mm_shared_space);
#endif
}

/**********************************************************************
 * Core Initialization and Termination.
 **********************************************************************/

static void
mm_core_boot_init(struct mm_core *core)
{
	if (MM_CORE_IS_PRIMARY(core)) {
		// Call the start hooks on the primary core.
		mm_timer_init(&core->time_manager, &core->space.arena);
		mm_hook_call(&mm_core_start_hook, false);
		mm_cdata_summary(&mm_core_domain);

		mm_thread_domain_barrier();
	} else {
		// Secondary cores have to wait until the primary core runs
		// the start hooks that initialize shared resources.
		mm_thread_domain_barrier();

		mm_timer_init(&core->time_manager, &core->space.arena);
	}
}

static void
mm_core_boot_term(struct mm_core *core)
{
	// Call the stop hooks on the primary core.
	if (MM_CORE_IS_PRIMARY(core))
		mm_hook_call(&mm_core_stop_hook, false);

	mm_timer_term(&core->time_manager);

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

	// Set the thread-specific data.
	mm_core = core;

	// Set pointer to the running task.
	mm_core->task = mm_core->boot;
	mm_core->task->state = MM_TASK_RUNNING;

#if ENABLE_TRACE
	mm_trace_context_prepare(&core->task->trace, "[%s][%d %s]",
				 mm_thread_getname(mm_thread_self()),
				 mm_task_getid(core->task),
				 mm_task_getname(core->task));
#endif

	// Initialize per-core resources.
	mm_core_boot_init(core);

	// Start master & dealer tasks.
	mm_core_start_basic_tasks(core);

	// Run the other tasks while there are any.
	mm_task_yield();

	// Destroy per-core resources.
	mm_core_boot_term(core);

	// Invalidate the boot task.
	mm_core->task->state = MM_TASK_INVALID;
	mm_core->task = NULL;

	// Abandon the core.
	mm_core = NULL;

	LEAVE();
	return 0;
}

static void
mm_core_init_single(struct mm_core *core, uint32_t nworkers_max)
{
	ENTER();

	mm_private_space_prepare(&core->space, true);

	mm_runq_prepare(&core->runq);
	mm_list_init(&core->idle);
	mm_list_init(&core->dead);
	mm_queue_init(&core->workq);

	mm_wait_cache_prepare(&core->wait_cache);

	core->nwork = 0;
	core->nidle = 0;
	core->nworkers = 0;
	core->nworkers_max = nworkers_max;

	core->master = NULL;

	core->stop = false;

	mm_listener_prepare(&core->listener);

	mm_ring_spsc_prepare(&core->sched, MM_CORE_SCHED_RING_SIZE, MM_RING_LOCKED_PUT);
	mm_ring_spsc_prepare(&core->inbox, MM_CORE_INBOX_RING_SIZE, MM_RING_LOCKED_PUT);
	mm_ring_spsc_prepare(&core->chunks, MM_CORE_CHUNK_RING_SIZE, MM_RING_LOCKED_PUT);

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
	mm_core_t core_id = mm_core_getid(core);
	while (mm_core_has_work(core)) {
		struct mm_work *work = mm_core_get_work(core);
		mm_work_destroy_low(core_id, work);
	}
}

static void
mm_core_term_inbox(struct mm_core *core)
{
	mm_core_t core_id = mm_core_getid(core);

	struct mm_work *work;
	while (mm_ring_spsc_get(&core->inbox, (void **) &work))
		mm_work_destroy_low(core_id, work);
}

static void
mm_core_term_single(struct mm_core *core)
{
	ENTER();

	mm_core_term_work(core);
	mm_core_term_inbox(core);

	mm_listener_cleanup(&core->listener);

	mm_wait_cache_cleanup(&core->wait_cache);

	mm_task_destroy(core->boot);

	// Flush logs before memory space with possible log chunks is unmapped.
	mm_log_relay();
	mm_log_flush();

	mm_private_space_cleanup(&core->space);

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

static bool
mm_core_yield(void)
{
	if (mm_core_self() == NULL)
		return false;

	mm_task_yield();
	return true;
}

#if ENABLE_TRACE

static struct mm_trace_context *
mm_core_gettracecontext(void)
{
	struct mm_core *core = mm_core_self();
	if (core != NULL)
		return &core->task->trace;
	struct mm_thread *thread = mm_thread_self();
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

	// Find the number of CPU cores.
	mm_core_num = mm_core_get_ncpu();
	ASSERT(mm_core_num > 0);
	if (mm_core_num == 1)
		mm_brief("Running on 1 core.");
	else
		mm_brief("Running on %d cores.", mm_core_num);

	mm_memory_init(mm_core_chunk_select,
		       mm_core_chunk_alloc,
		       mm_core_chunk_free);
	mm_thread_init();
	mm_clock_init();

	mm_shared_space_init();
	mm_event_init();
	mm_net_init();

	mm_task_init();
	mm_port_init();
	mm_wait_init();
	mm_future_init();
	mm_work_init();

	mm_backoff_set_yield(mm_core_yield);
#if ENABLE_TRACE
	mm_trace_set_getcontext(mm_core_gettracecontext);
#endif
	mm_domain_prepare(&mm_core_domain, "core", mm_core_num);
	mm_dispatch_prepare(&mm_core_dispatch);

	mm_core_set = mm_global_aligned_alloc(MM_CACHELINE, mm_core_num * sizeof(struct mm_core));
	for (mm_core_t i = 0; i < mm_core_num; i++)
		mm_core_init_single(&mm_core_set[i], MM_DEFAULT_WORKERS);

	mm_bitset_prepare(&mm_core_event_affinity, &mm_common_space.arena, mm_core_num);

	LEAVE();
}

void
mm_core_term(void)
{
	ENTER();
	ASSERT(mm_core_num > 0);

	mm_bitset_cleanup(&mm_core_event_affinity, &mm_common_space.arena);

	for (mm_core_t i = 0; i < mm_core_num; i++)
		mm_core_term_single(&mm_core_set[i]);
	mm_global_free(mm_core_set);

	mm_domain_cleanup(&mm_core_domain);

	mm_core_free_hooks();

	mm_task_term();
	mm_port_term();
	mm_wait_term();

	mm_net_term();

	// Flush logs before memory space with possible log chunks is unmapped.
	mm_log_relay();
	mm_log_flush();

	mm_shared_space_term();
	mm_memory_term();

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

	// Set core thread attributes.
	for (mm_core_t i = 0; i < mm_core_num; i++) {
		struct mm_core *core = &mm_core_set[i];
		mm_domain_setstack(&mm_core_domain, i,
				   (char *) core->boot->stack_base + MM_PAGE_SIZE,
				   core->boot->stack_size - MM_PAGE_SIZE);
		mm_domain_setcputag(&mm_core_domain, i, i);
	}

	mm_domain_start(&mm_core_domain, mm_core_boot);

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
	mm_domain_join(&mm_core_domain);

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
		mm_listener_notify(&core->listener, &mm_core_dispatch);
	}

	LEAVE();
}
