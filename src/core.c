/*
 * core.c - MainMemory core.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#include "core.h"

#include "sched.h"
#include "port.h"
#include "task.h"
#include "work.h"
#include "util.h"

#define MM_DEFAULT_WORKERS 512

__thread struct mm_core *mm_core;

/**********************************************************************
 * Master task.
 **********************************************************************/

static struct mm_task *
mm_core_get_worker(mm_task_flags_t flags, mm_routine routine, uintptr_t routine_arg)
{
	return mm_task_create("worker", flags, routine, routine_arg);
}

static void
mm_core_master_loop(uintptr_t arg __attribute__((unused)))
{
	ENTER();

	while (!mm_core->stop) {
		struct mm_work *work = mm_work_get();

		for (uint32_t i = 0; i < work->count; i++) {
			struct mm_task *task = mm_core_get_worker(
				work->flags,
				work->routine,
				work->items[i]);
			mm_sched_run(task);
		}

		mm_work_destroy(work);
	}

	LEAVE();
}

static void
mm_core_start_master(struct mm_core *core)
{
	ENTER();

	core->master = mm_task_create("master", 0, mm_core_master_loop, 0);
	mm_sched_run(core->master);

	LEAVE();
}

static void
mm_core_stop_master(struct mm_core *core)
{
	ENTER();

	core->stop = true;

	LEAVE();
}

/**********************************************************************
 * Core initialization and termination.
 **********************************************************************/

static struct mm_core *
mm_core_create(uint32_t nworkers_max)
{
	ENTER();

	struct mm_core *core = mm_alloc(sizeof(struct mm_core));

	core->master = NULL;
	core->nworkers = 0;
	core->nworkers_max = nworkers_max;
	mm_runq_init(&core->run_queue);
	mm_list_init(&core->dead_list);
	core->stop = false;

	LEAVE();
	return core;
}

static void
mm_core_destroy(struct mm_core *core)
{
	ENTER();

	mm_free(core);

	LEAVE();
}

void
mm_core_init()
{
	ENTER();

	mm_core = mm_core_create(MM_DEFAULT_WORKERS);

	mm_work_init();
	mm_task_init();
	mm_port_init();

	LEAVE();
}

void
mm_core_term()
{
	ENTER();

	mm_task_term();
	mm_work_term();
	mm_port_term();
	
	mm_core_destroy(mm_core);

	LEAVE();
}

void
mm_core_start(void)
{
	ENTER();

	mm_core_start_master(mm_core);
	mm_sched_start();

	LEAVE();
}

void
mm_core_stop(void)
{
	ENTER();

	mm_core_stop_master(mm_core);

	LEAVE();
}
