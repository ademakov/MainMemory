/*
 * base/base.c - Base library setup.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "base/base.h"
#include "base/topology.h"
#include "base/log/debug.h"
#include "base/log/log.h"
#include "base/log/trace.h"
#include "base/mem/memory.h"
#include "base/sys/clock.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"
#include "base/util/exit.h"

#include <unistd.h>

struct mm_domain mm_regular_domain = { .nthreads = 0 };

void
mm_base_init(struct mm_base_params *params)
{
	ENTER();

	int ncpus = mm_topology_getncpus();
	ASSERT(ncpus > 0);

	const char *regular_name = "regular thread";
	if (params != NULL && params->regular_name != NULL)
		regular_name = params->regular_name;

	mm_memory_init();
	mm_thread_init();
	mm_clock_init();

	mm_domain_prepare(&mm_regular_domain, regular_name,
			  ncpus, true, params->thread_notify);
	for (mm_thread_t i = 0; i < ncpus; i++) {
		mm_domain_setcputag(&mm_regular_domain, i, i);
	}

	LEAVE();
}

void
mm_base_term(void)
{
	ENTER();

	mm_domain_cleanup(&mm_regular_domain);
	mm_memory_term();

	LEAVE();
}

void
mm_base_loop(mm_routine_t thread_routine)
{
	ENTER();

	// Start regular threads.
	mm_domain_start(&mm_regular_domain, thread_routine);

	// Loop until stopped.
	while (!mm_exit_test()) {
		size_t logged = mm_log_flush();
		usleep(logged ? 30000 : 3000000);
	}

	// Wait for regular threads completion.
	mm_domain_join(&mm_regular_domain);

	LEAVE();
}
