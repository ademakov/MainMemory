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
#include "base/logger.h"
#include "base/report.h"
#include "base/settings.h"
#include "base/topology.h"
#include "base/event/event.h"
#include "base/memory/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"
#include "base/util/hook.h"

#include <unistd.h>

uint16_t mm_ncpus = 0;
struct mm_domain *mm_regular_domain = NULL;

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
mm_base_validate_nthreads(uint32_t n)
{
#if ENABLE_SMP
	return n <= UINT16_MAX;
#else
	return n == 1;
#endif
}

void
mm_base_init(void)
{
	ENTER();

	uint32_t nthreads = mm_settings_get_uint32("thread-number", 0);
	if (nthreads != 0 && !mm_base_validate_nthreads(nthreads)) {
		mm_error(0, "ignore unsupported thread number value: %u", nthreads);
		nthreads = 0;
	}

	// Determine the machine topology.
	if (nthreads == 0)
		mm_ncpus = mm_topology_getncpus();
	else
		mm_ncpus = nthreads;

	// Initialize basic subsystems.
	mm_memory_init();
	mm_thread_init();
	mm_cksum_init();
	mm_clock_init();

	// Invoke registered start hooks.
	mm_call_common_start_hooks();

	LEAVE();
}

void
mm_base_term(void)
{
	ENTER();

	// Free regular thread domain.
	mm_domain_destroy(mm_regular_domain);

	// Invoke registered stop hooks.
	mm_call_common_stop_hooks();
	// Free all registered hooks.
	mm_free_hooks();

	// Cleanup memory spaces.
	mm_memory_term();

	LEAVE();
}

void NONNULL(1)
mm_base_loop(struct mm_base_params *params)
{
	ENTER();

	// Determine the domain name.
	const char *name = "regular";
	if (params->regular_name != NULL)
		name = params->regular_name;

	// Set regular domain attributes.
	struct mm_domain_attr attr;
	mm_domain_attr_prepare(&attr);
	mm_domain_attr_setname(&attr, name);
	mm_domain_attr_setnumber(&attr, mm_ncpus);
	mm_domain_attr_setstacksize(&attr, params->thread_stack_size);
	mm_domain_attr_setguardsize(&attr, params->thread_guard_size);
	mm_domain_attr_setspace(&attr, true);
	mm_domain_attr_setdomainqueue(&attr, mm_ncpus * 32);
	mm_domain_attr_setthreadqueue(&attr, mm_ncpus * 32);

	bool thread_affinity = mm_settings_get_bool("thread-affinity", false);
	if (thread_affinity) {
		mm_verbose("set thread affinity");
		for (mm_thread_t i = 0; i < mm_ncpus; i++)
			mm_domain_attr_setcputag(&attr, i, i);
	}

	// Start regular threads.
	mm_regular_domain = mm_domain_create(&attr, params->thread_routine);

	// Release domain creation attributes.
	mm_domain_attr_cleanup(&attr);

	// Loop until stopped.
	mm_log_relay();
	while (!mm_exit_test()) {
		size_t logged = mm_log_flush();
		usleep(logged ? 30000 : 3000000);
	}

	// Wait for regular threads completion.
	mm_domain_join(mm_regular_domain);

	mm_log_str("exiting...\n");

	LEAVE();
}
