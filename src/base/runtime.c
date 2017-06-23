/*
 * base/runtime.c - Base library runtime.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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
#include "base/exit.h"
#include "base/list.h"
#include "base/daemon.h"
#include "base/logger.h"
#include "base/report.h"
#include "base/settings.h"
#include "base/topology.h"
#include "base/event/dispatch.h"
#include "base/event/event.h"
#include "base/fiber/core.h"
#include "base/memory/global.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"
#include "base/util/hook.h"

#include <unistd.h>

mm_thread_t mm_regular_nthreads = 0;
struct mm_domain *mm_regular_domain = NULL;

// Event dispatch for regular thread domain.
static struct mm_event_dispatch mm_regular_dispatch;

// Run in a daemon mode.
static bool mm_daemonize = false;
static char *mm_log_file_name = NULL;

// Runtime stop flag.
static int mm_stop_flag = 0;

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
mm_call_common_start_hooks(void)
{
	mm_hook_call(&mm_common_start_hook, false);
}
static void
mm_call_common_stop_hooks(void)
{
	mm_hook_call(&mm_common_stop_hook, false);
}

void
mm_call_regular_start_hooks(void)
{
	mm_hook_call(&mm_regular_start_hook, false);
}
void
mm_call_regular_stop_hooks(void)
{
	mm_hook_call(&mm_regular_stop_hook, false);
}

void
mm_call_regular_thread_start_hooks(void)
{
	mm_hook_call(&mm_regular_thread_start_hook, false);
}
void
mm_call_regular_thread_stop_hooks(void)
{
	mm_hook_call(&mm_regular_thread_stop_hook, false);
}

/**********************************************************************
 * General runtime routines.
 **********************************************************************/

static bool
mm_validate_nthreads(uint32_t n)
{
#if ENABLE_SMP
	return n < MM_THREAD_SELF;
#else
	return n == 1;
#endif
}

static void
mm_regular_start(void)
{
	// Allocate event dispatch memory and system resources.
	mm_event_dispatch_prepare(&mm_regular_dispatch, mm_regular_domain,
				  mm_regular_domain->nthreads, mm_regular_domain->threads);
}

static void
mm_regular_stop(void)
{
	// Print statistics.
	mm_event_dispatch_stats(&mm_regular_dispatch);
	mm_lock_stats();

	// Release event dispatch memory and system resources.
	mm_event_dispatch_cleanup(&mm_regular_dispatch);
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

	// Parse the command line arguments.
	mm_args_init(argc, argv, ninfo, info);

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
mm_runtime_init(void)
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

	// Initialize basic subsystems.
	mm_memory_init();
	mm_thread_init();
	mm_cksum_init();
	mm_clock_init();

	// Setup basic start and stop hooks for regular domain.
	mm_regular_start_hook_0(mm_regular_start);
	mm_regular_stop_hook_0(mm_regular_stop);

	LEAVE();
}

void
mm_start(void)
{
	ENTER();

	// Daemonize if needed.
	if (mm_daemonize) {
		mm_daemon_start();
		mm_daemon_stdio(NULL, mm_log_file_name);
		mm_daemon_notify();
	}

	// Initialize fiber subsystem.
	mm_core_init();

	// Invoke registered start hooks.
	mm_call_common_start_hooks();

	// Set regular domain attributes.
	struct mm_domain_attr attr;
	mm_domain_attr_prepare(&attr);
	mm_domain_attr_setname(&attr, "regular");
	mm_domain_attr_setnumber(&attr, mm_regular_nthreads);
	mm_domain_attr_setstacksize(&attr, MM_PAGE_SIZE); // enough for fiber bootstrap
	mm_domain_attr_setguardsize(&attr, MM_PAGE_SIZE);
	mm_domain_attr_setspace(&attr, true);
	mm_domain_attr_setdomainqueue(&attr, mm_regular_nthreads * 32);
	mm_domain_attr_setthreadqueue(&attr, mm_regular_nthreads * 32);

	bool thread_affinity = mm_settings_get_bool("thread-affinity", false);
	if (thread_affinity) {
		mm_verbose("set thread affinity");
		for (mm_thread_t i = 0; i < mm_regular_nthreads; i++)
			mm_domain_attr_setcputag(&attr, i, i);
	}

	// Start regular threads.
	mm_regular_domain = mm_domain_create(&attr, mm_core_boot);

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
	mm_call_common_stop_hooks();
	// Free all registered hooks.
	mm_free_hooks();

	// Cleanup fiber subsystem.
	mm_core_term();
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

	mm_core_stop();

	mm_memory_store(mm_stop_flag, 1);

	LEAVE();
}
