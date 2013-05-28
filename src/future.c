/*
 * future.c - MainMemory delayed computation.
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

#include "future.h"

#include "alloc.h"
#include "core.h"
#include "pool.h"
#include "sched.h"
#include "trace.h"
#include "work.h"


void
mm_future_init(void)
{
	ENTER();

	mm_pool_init(&mm_core->future_pool, "future",
		     &mm_alloc_core, sizeof(struct mm_future));

	LEAVE();
}

void
mm_future_term(void)
{
	ENTER();

	mm_pool_discard(&mm_core->future_pool);

	LEAVE();
}

struct mm_future *
mm_future_create(const char *name __attribute__((unused)), mm_task_flags_t flags,
		 mm_routine_t start, uintptr_t start_arg)
{
	ENTER();

	struct mm_future *future = mm_pool_alloc(&mm_core->future_pool);
	future->status = MM_FUTURE_CREATED;
	future->result = MM_TASK_UNRESOLVED;
	future->flags = flags;
	future->start = start;
	future->start_arg = start_arg;

	mm_list_init(&future->blocked_tasks);

	LEAVE();
	return future;
}

void
mm_future_destroy(struct mm_future *future)
{
	ENTER();
	ASSERT(future->status != MM_FUTURE_STARTED);
	ASSERT(mm_list_empty(&future->blocked_tasks));

	mm_pool_free(&mm_core->future_pool, future);

	LEAVE();
}

static void
mm_future_cleanup(struct mm_future *future)
{
	ENTER();

	if (future->status == MM_FUTURE_STARTED) {
		if ((future->task->flags & MM_TASK_CANCEL_OCCURRED) != 0) {
			future->status = MM_FUTURE_CANCELED;
		} else {
			future->status = MM_FUTURE_COMPLETED;
			future->result = future->task->result;
		}

		mm_task_broadcast(&future->blocked_tasks);
	}
	future->task = NULL;

	LEAVE();
}

static mm_result_t
mm_future_routine(uintptr_t arg)
{
	ENTER();

	struct mm_future *future = (struct mm_future *) arg;
	if (likely(future->status == MM_FUTURE_STARTED)) {
		// Ensure cleanup on exit.
		mm_task_cleanup_push(mm_future_cleanup, future);

		// Execute the task bound to the future.
		future->task = mm_running_task;
		future->start(future->start_arg);

		// Cleanup on return.
		mm_task_cleanup_pop(true);
	}

	LEAVE();
	return 0;
}

void
mm_future_start(struct mm_future *future)
{
	ENTER();

	if (future->status == MM_FUTURE_CREATED) {
		future->status = MM_FUTURE_STARTED;
		mm_work_add(0, mm_future_routine, (uintptr_t) future);
	}

	LEAVE();
}

void
mm_future_wait(struct mm_future *future)
{
	ENTER();

	mm_future_start(future);

	int cp = mm_task_enter_cancel_point();
	while (future->status == MM_FUTURE_STARTED) {
		mm_task_wait_fifo(&future->blocked_tasks);
	}
	mm_task_leave_cancel_point(cp);

	ENTER();
}
