/*
 * base/runtime.c - Base library runtime.
 *
 * Copyright (C) 2015-2019  Aleksey Demakov
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

#include "base/runtime.h"
#include "base/cksum.h"
#include "base/clock.h"
#include "base/context.h"
#include "base/daemon.h"
#include "base/exit.h"
#include "base/list.h"
#include "base/logger.h"
#include "base/report.h"
#include "base/settings.h"
#include "base/topology.h"
#include "base/event/dispatch.h"
#include "base/event/event.h"
#include "base/event/listener.h"
#include "base/fiber/fiber.h"
#include "base/fiber/future.h"
#include "base/fiber/strand.h"
#include "base/memory/global.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"
#include "base/util/hook.h"

#include <unistd.h>

// The number of regular threads.
static mm_thread_t mm_regular_nthreads = 0;

// The set of thread execution contexts.
static struct mm_context **mm_context_table = NULL;
// Temporary storage for task statistics.
static struct mm_context_stats *mm_context_stats_store = NULL;
// Temporary storage for task statistics.
static struct mm_task_stats *mm_task_stats_store = NULL;

// The domain of regular threads.
static struct mm_domain *mm_regular_domain = NULL;

// Strands for regular domain threads.
static struct mm_strand *mm_regular_strands;

// Event dispatch for regular thread domain.
static struct mm_event_dispatch mm_regular_dispatch;

// Run in a daemon mode.
static bool mm_daemonize = false;
static char *mm_log_file_name = NULL;

// Runtime stop flag.
static int mm_stop_flag = 0;

/**********************************************************************
 * Runtime information.
 **********************************************************************/

mm_thread_t
mm_number_of_regular_domains(void)
{
	return 1;
}

mm_thread_t
mm_number_of_regular_threads(void)
{
	return mm_regular_nthreads;
}

struct mm_domain *
mm_domain_ident_to_domain(mm_thread_t ident)
{
	if (ident != 0)
		return NULL;
	return mm_regular_domain;
}

struct mm_thread *
mm_thread_ident_to_thread(mm_thread_t ident)
{
	if (ident >= mm_regular_nthreads)
		return NULL;
	return mm_domain_getthread(mm_regular_domain, ident);
}

struct mm_context *
mm_thread_ident_to_context(mm_thread_t ident)
{
	if (ident >= mm_regular_nthreads)
		return NULL;
	return mm_context_table[ident];
}

struct mm_strand *
mm_thread_ident_to_strand(mm_thread_t ident)
{
	if (ident >= mm_regular_nthreads)
		return NULL;
	return &mm_regular_strands[ident];
}

struct mm_event_dispatch *
mm_domain_ident_to_event_dispatch(mm_thread_t ident)
{
	if (ident != 0)
		return NULL;
	return &mm_regular_dispatch;
}

struct mm_event_dispatch *
mm_thread_ident_to_event_dispatch(mm_thread_t ident)
{
	if (ident >= mm_regular_nthreads)
		return NULL;
	return &mm_regular_dispatch;
}

struct mm_event_listener *
mm_thread_ident_to_event_listener(mm_thread_t ident)
{
	if (ident >= mm_regular_nthreads)
		return NULL;
	return &mm_regular_dispatch.listeners[ident];
}

/**********************************************************************
 * Runtime start and stop hooks.
 **********************************************************************/

static struct mm_queue MM_QUEUE_INIT(mm_common_start_hook);
static struct mm_queue MM_QUEUE_INIT(mm_common_stop_hook);

static struct mm_queue MM_QUEUE_INIT(mm_regular_start_hook);
static struct mm_queue MM_QUEUE_INIT(mm_regular_stop_hook);

static struct mm_queue MM_QUEUE_INIT(mm_regular_thread_start_hook);
static struct mm_queue MM_QUEUE_INIT(mm_regular_thread_stop_hook);

static void
mm_free_hooks(void)
{
	mm_hook_free(&mm_common_start_hook);
	mm_hook_free(&mm_common_stop_hook);

	mm_hook_free(&mm_regular_start_hook);
	mm_hook_free(&mm_regular_stop_hook);

	mm_hook_free(&mm_regular_thread_start_hook);
	mm_hook_free(&mm_regular_thread_stop_hook);
}

void NONNULL(1)
mm_common_start_hook_0(void (*proc)(void))
{
	mm_hook_tail_proc(&mm_common_start_hook, proc);
}
void NONNULL(1)
mm_common_start_hook_1(void (*proc)(void *), void *data)
{
	mm_hook_tail_data_proc(&mm_common_start_hook, proc, data);
}

void NONNULL(1)
mm_common_stop_hook_0(void (*proc)(void))
{
	mm_hook_head_proc(&mm_common_stop_hook, proc);
}
void NONNULL(1)
mm_common_stop_hook_1(void (*proc)(void *), void *data)
{
	mm_hook_head_data_proc(&mm_common_stop_hook, proc, data);
}

void NONNULL(1)
mm_regular_start_hook_0(void (*proc)(void))
{
	mm_hook_tail_proc(&mm_regular_start_hook, proc);
}
void NONNULL(1)
mm_regular_start_hook_1(void (*proc)(void *), void *data)
{
	mm_hook_tail_data_proc(&mm_regular_start_hook, proc, data);
}

void NONNULL(1)
mm_regular_stop_hook_0(void (*proc)(void))
{
	mm_hook_head_proc(&mm_regular_stop_hook, proc);
}
void NONNULL(1)
mm_regular_stop_hook_1(void (*proc)(void *), void *data)
{
	mm_hook_head_data_proc(&mm_regular_stop_hook, proc, data);
}

void NONNULL(1)
mm_regular_thread_start_hook_0(void (*proc)(void))
{
	mm_hook_tail_proc(&mm_regular_thread_start_hook, proc);
}
void NONNULL(1)
mm_regular_thread_start_hook_1(void (*proc)(void *), void *data)
{
	mm_hook_tail_data_proc(&mm_regular_thread_start_hook, proc, data);
}

void NONNULL(1)
mm_regular_thread_stop_hook_0(void (*proc)(void))
{
	mm_hook_head_proc(&mm_regular_thread_stop_hook, proc);
}
void NONNULL(1)
mm_regular_thread_stop_hook_1(void (*proc)(void *), void *data)
{
	mm_hook_head_data_proc(&mm_regular_thread_stop_hook, proc, data);
}

static void
mm_common_call_start_hooks(void)
{
	mm_hook_call(&mm_common_start_hook, false);
}
static void
mm_common_call_stop_hooks(void)
{
	mm_hook_call(&mm_common_stop_hook, false);
}

static void
mm_regular_call_start_hooks(void)
{
	mm_hook_call(&mm_regular_start_hook, false);
}
static void
mm_regular_call_stop_hooks(void)
{
	mm_hook_call(&mm_regular_stop_hook, false);
}

static void
mm_regular_call_thread_start_hooks(void)
{
	mm_hook_call(&mm_regular_thread_start_hook, false);
}
static void
mm_regular_call_thread_stop_hooks(void)
{
	mm_hook_call(&mm_regular_thread_stop_hook, false);
}

/**********************************************************************
 * Regular threads entry point.
 **********************************************************************/

#if ENABLE_SMP
# define MM_STRAND_IS_PRIMARY(strand)	(strand == mm_regular_strands)
#else
# define MM_STRAND_IS_PRIMARY(strand)	(true)
#endif

static void
mm_regular_boot_call_start_hooks(struct mm_strand *strand)
{
	if (MM_STRAND_IS_PRIMARY(strand)) {
		// Call the start hooks on the primary strand.
		mm_regular_call_start_hooks();
		mm_thread_local_summary(mm_domain_selfptr());
		mm_regular_call_thread_start_hooks();

		mm_domain_barrier();
	} else {
		// Secondary strands have to wait until the primary strand runs
		// the start hooks that initialize shared resources.
		mm_domain_barrier();

		mm_regular_call_thread_start_hooks();
	}
}

static void
mm_regular_boot_call_stop_hooks(struct mm_strand *strand)
{
	mm_domain_barrier();

	// Call the stop hooks on the primary strand.
	if (MM_STRAND_IS_PRIMARY(strand))
		mm_regular_call_stop_hooks();

	mm_regular_call_thread_stop_hooks();
}

/* A per-strand thread entry point. */
static mm_value_t
mm_regular_boot(mm_value_t arg)
{
	ENTER();

	// Allocate and setup the execution context.
	struct mm_context *const context = mm_regular_alloc(sizeof(struct mm_context));
	mm_context_prepare(context, arg, mm_regular_nthreads * 32);
	mm_context_table[arg] = context;
	__mm_context_self = context;

	struct mm_strand *const strand = context->strand;
	strand->thread = mm_thread_selfptr();

	// Set pointer to the running fiber.
	context->fiber = strand->boot;
	context->fiber->state = MM_FIBER_RUNNING;

#if ENABLE_TRACE
	mm_trace_context_prepare(&context->fiber->trace, "[%s %s]",
				 mm_thread_getname(strand->thread),
				 mm_fiber_getname(context->fiber));
#endif

	// Initialize per-strand resources.
	mm_regular_boot_call_start_hooks(strand);

	// Run fibers machinery for a while.
	context->status = MM_CONTEXT_RUNNING;
	mm_strand_loop(strand, context);
	context->status = MM_CONTEXT_PENDING;

	// Destroy per-strand resources.
	mm_regular_boot_call_stop_hooks(strand);

	// Invalidate the boot fiber.
	context->fiber->state = MM_FIBER_INVALID;
	context->fiber = NULL;

	// Release the execution context.
	__mm_context_self = NULL;
	mm_context_table[arg] = NULL;
	mm_context_stats_store[arg] = context->stats;
	mm_task_stats_store[arg] = context->tasks.stats;
	mm_context_cleanup(context);
	mm_regular_free(context);

	LEAVE();
	return 0;
}

/**********************************************************************
 * Runtime control routines.
 **********************************************************************/

static bool
mm_validate_nthreads(uint32_t n)
{
#if ENABLE_SMP
	return n < MM_THREAD_NONE;
#else
	return n == 1;
#endif
}

static void
mm_common_start(void)
{
	ENTER();

	// Allocate the storage for thread execution contexts.
	mm_context_table = mm_common_calloc(mm_regular_nthreads, sizeof(struct mm_context *));
	mm_context_stats_store = mm_common_calloc(mm_regular_nthreads, sizeof(struct mm_context_stats));
	mm_task_stats_store = mm_common_calloc(mm_regular_nthreads, sizeof(struct mm_task_stats));

	// Allocate a fiber strand for each regular thread.
	mm_regular_strands = mm_common_aligned_alloc(MM_CACHELINE, mm_regular_nthreads * sizeof(struct mm_strand));
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
		mm_strand_prepare(&mm_regular_strands[i]);

	// Allocate event dispatch memory and system resources.
	struct mm_event_dispatch_attr attr;
	mm_event_dispatch_attr_prepare(&attr);
	mm_event_dispatch_attr_setlisteners(&attr, mm_regular_nthreads);
	mm_event_dispatch_attr_setlockspinlimit(&attr, mm_settings_get_uint32("event-lock-spin-limit", 1));
	mm_event_dispatch_attr_setpollspinlimit(&attr, mm_settings_get_uint32("event-poll-spin-limit", 4));
#if DISPATCH_ATTRS
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
		mm_event_dispatch_attr_setlistenerxxx(&attr, i, xxx);
#endif
	mm_event_dispatch_prepare(&mm_regular_dispatch, &attr);
	mm_event_dispatch_attr_cleanup(&attr);

	LEAVE();
}

static void
mm_common_stop(void)
{
	ENTER();

	// Print statistics.
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++) {
		mm_strand_report_stats(&mm_regular_strands[i]);
		mm_task_report_stats(&mm_task_stats_store[i]);
		mm_context_report_stats(&mm_context_stats_store[i]);
	}
	mm_event_dispatch_stats(&mm_regular_dispatch);
	mm_lock_stats();

	// Release event dispatch memory and system resources.
	mm_event_dispatch_cleanup(&mm_regular_dispatch);

	// Cleanup fiber subsystem.
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
		mm_strand_cleanup(&mm_regular_strands[i]);
	mm_common_free(mm_regular_strands);

	// Release the storage for thread execution contexts.
	mm_common_free(mm_context_table);

	LEAVE();
}

static void
mm_daemon_cleanup(void)
{
	if (mm_log_file_name != NULL)
		mm_global_free(mm_log_file_name);
}

void NONNULL(2)
mm_init(int argc, char *argv[], size_t ninfo, const struct mm_args_info *info)
{
	ENTER();

	// Prepare for graceful exit.
	mm_exit_init();

	// Prepare the settings storage.
	mm_settings_init();
	mm_settings_set_info("event-lock-spin-limit", MM_SETTINGS_REGULAR);
	mm_settings_set_info("event-poll-spin-limit", MM_SETTINGS_REGULAR);
	mm_settings_set_info("thread-affinity", MM_SETTINGS_BOOLEAN);
	mm_settings_set_info("thread-number", MM_SETTINGS_REGULAR);

	// Parse the command line arguments.
	mm_args_init(argc, argv, ninfo, info);
	int verbosity = mm_args_get_verbosity_level();
	if (verbosity)
		mm_set_verbosity_level(verbosity);

	// Initialize the most basic facilities that do not have any
	// dependencies.
	mm_clock_init();
	mm_cksum_init();
	mm_thread_init();

	// Initialize the memory spaces.
	mm_memory_init();

	// Setup the basic common start hook.
	mm_common_start_hook_0(mm_common_start);
	mm_common_stop_hook_0(mm_common_stop);

	// Register hooks required by various subsystems.
	mm_wait_init();
	mm_future_init();

	LEAVE();
}

void
mm_set_daemon_mode(const char *log_file)
{
	ENTER();

	mm_daemonize = true;

	VERIFY(mm_log_file_name == NULL);
	if (log_file != NULL) {
		mm_log_file_name = mm_global_strdup(log_file);
		mm_atexit(mm_daemon_cleanup);
	}

	LEAVE();
}

void
mm_start(void)
{
	ENTER();

	// Try to get thread number parameter possibly provided by user.
	uint32_t nthreads = mm_settings_get_uint32("thread-number", 0);
	if (nthreads != 0 && !mm_validate_nthreads(nthreads)) {
		mm_error(0, "ignore unsupported thread number value: %u", nthreads);
		nthreads = 0;
	}

	// Determine the machine topology.
	uint16_t ncpus = mm_topology_getncpus();
	mm_brief("running on %d cores", ncpus);

	// Determine the number of regular threads.
	mm_regular_nthreads = nthreads ? nthreads : ncpus;
	if (mm_regular_nthreads == 1)
		mm_brief("using 1 thread");
	else
		mm_brief("using %d threads", mm_regular_nthreads);

	// Calibrate internal clock.
	mm_timepiece_init();

	// Daemonize if needed.
	if (mm_daemonize) {
		mm_daemon_start();
		mm_daemon_stdio(NULL, mm_log_file_name);
		mm_daemon_notify();
	}

	// Invoke registered start hooks.
	mm_common_call_start_hooks();

	// Set regular domain attributes.
	struct mm_domain_attr attr;
	mm_domain_attr_prepare(&attr);
	mm_domain_attr_setname(&attr, "regular");
	mm_domain_attr_setsize(&attr, mm_regular_nthreads);
	mm_domain_attr_setstacksize(&attr, MM_PAGE_SIZE); // enough for fiber bootstrap
	mm_domain_attr_setguardsize(&attr, MM_PAGE_SIZE);
	mm_domain_attr_setspace(&attr, true);

	bool thread_affinity = mm_settings_get_bool("thread-affinity", false);
	if (thread_affinity) {
		mm_verbose("set thread affinity");
		for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
			mm_domain_attr_setcputag(&attr, i, i);
	}

	// Start regular threads.
	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
		mm_domain_attr_setarg(&attr, i, i);
	mm_regular_domain = mm_domain_create(&attr, mm_regular_boot);
	VERIFY(mm_domain_ident(mm_regular_domain) == 0);
	VERIFY(mm_domain_first_thread_ident(mm_regular_domain) == 0);

	// Release domain creation attributes.
	mm_domain_attr_cleanup(&attr);

	// Loop until stopped.
	mm_log_relay();
	while (mm_memory_load(mm_stop_flag) == 0) {
		size_t logged = mm_log_flush();
		usleep(logged ? 30000 : 3000000);
	}

	mm_log_str("exiting...\n");

	// Wait for regular threads completion.
	mm_domain_join(mm_regular_domain);

	// Invoke registered stop hooks.
	mm_common_call_stop_hooks();
	// Free all registered hooks.
	mm_free_hooks();
	// Free regular thread domain.
	mm_domain_destroy(mm_regular_domain);
	// Cleanup memory spaces.
	mm_memory_term();

	LEAVE();
}

void
mm_stop(void)
{
	ENTER();

	for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
		mm_strand_stop(&mm_regular_strands[i]);
	mm_memory_store(mm_stop_flag, 1);

	LEAVE();
}
