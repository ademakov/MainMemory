/*
 * sched.c - MainMemory task scheduler.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#include "sched.h"

#include "core.h"
#include "runq.h"
#include "task.h"
#include "trace.h"

/* The currently running task. */
__thread struct mm_task *mm_running_task = NULL;

/* Switch to the next task in the run queue. */
static void
mm_sched_switch(mm_task_state_t state)
{
	ASSERT(mm_running_task->state == MM_TASK_RUNNING);

	struct mm_task *old_task;
	struct mm_task *new_task;

	old_task = mm_running_task;
	old_task->state = state;

	if (state == MM_TASK_PENDING) {
		mm_runq_put_task(&mm_core->run_queue, old_task);
		new_task = mm_runq_get_task(&mm_core->run_queue);
	} else {
		new_task = mm_runq_get_task(&mm_core->run_queue);
		if (unlikely(new_task == NULL)) {
			new_task = mm_core->boot;
		}
		if (state == MM_TASK_INVALID) {
			mm_task_recycle(old_task);
		}
	}

	mm_running_task = new_task;
	mm_running_task->state = MM_TASK_RUNNING;

	mm_stack_switch(&old_task->stack_ctx, &new_task->stack_ctx);
	mm_task_testcancel_asynchronous();
}

void
mm_sched_run(struct mm_task *task)
{
	ENTER();
	TRACE("enqueue task: [%d %s] %d", mm_task_id(task), task->name, task->state);
	ASSERT(task->state != MM_TASK_INVALID && task->state != MM_TASK_RUNNING);

	// As both the run and the wait queues use the same field for linking
	// running a task without dequeuing it from a wait queue would result
	// in the data corruption.
	ASSERT((task->flags & MM_TASK_WAITING) == 0);

	if (task->state != MM_TASK_PENDING) {
		mm_runq_put_task(&mm_core->run_queue, task);
		task->state = MM_TASK_PENDING;
	}

	LEAVE();
}

void
mm_sched_yield(void)
{
	ENTER();

	mm_sched_switch(MM_TASK_PENDING);

	LEAVE();
}

void
mm_sched_block(void)
{
	ENTER();

	mm_sched_switch(MM_TASK_BLOCKED);

	LEAVE();
}

void
mm_sched_abort(void)
{
	ENTER();

	mm_sched_switch(MM_TASK_INVALID);

	/* Must never get here after the switch above. */
	ABORT();

	LEAVE();
}
