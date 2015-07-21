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
#include "base/clock.h"
#include "base/topology.h"
#include "base/log/debug.h"
#include "base/log/log.h"
#include "base/log/trace.h"
#include "base/mem/memory.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"
#include "base/util/exit.h"

#include <unistd.h>

int mm_ncpus = 0;
struct mm_domain *mm_regular_domain = NULL;

void
mm_base_init(void)
{
	ENTER();

	// Determine machine topology.
	mm_ncpus = mm_topology_getncpus();
	ASSERT(mm_ncpus > 0);

	// Initialize basic subsystems.
	mm_memory_init();
	mm_thread_init();
	mm_clock_init();

	LEAVE();
}

void
mm_base_term(void)
{
	ENTER();

	mm_domain_destroy(mm_regular_domain);
	mm_memory_term();

	LEAVE();
}

void
mm_base_loop(struct mm_base_params *params)
{
	ENTER();

	// Determine the domain name.
	const char *name = "regular";
	if (params != NULL && params->regular_name != NULL)
		name = params->regular_name;

	// Set regular domain attributes.
	struct mm_domain_attr attr;
	mm_domain_attr_prepare(&attr);
	mm_domain_attr_setname(&attr, name);
	mm_domain_attr_setnumber(&attr, mm_ncpus);
	mm_domain_attr_setnotify(&attr, params->thread_notify);
	mm_domain_attr_setstacksize(&attr, params->thread_stack_size);
	mm_domain_attr_setguardsize(&attr, params->thread_guard_size);
	mm_domain_attr_setspace(&attr, true);
	mm_domain_attr_setdomainqueue(&attr, mm_ncpus * 32);
	mm_domain_attr_setthreadqueue(&attr, mm_ncpus * 32);
	for (mm_thread_t i = 0; i < mm_ncpus; i++) {
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

	LEAVE();
}
