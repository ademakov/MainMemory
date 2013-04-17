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

#include "arch.h"
#include "core.h"
#include "runq.h"
#include "task.h"
#include "util.h"

/* A pseudo-task that represents the initial process. */
static struct mm_task mm_null_task = {
	.state = MM_TASK_RUNNING,
	.name = "null-task",
};

/* The currently running task. */
__thread struct mm_task *mm_running_task = &mm_null_task;

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
			new_task = &mm_null_task;
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
	TRACE("enqueue task: %s %d", task->name, task->state);
	ASSERT(task->state != MM_TASK_INVALID && task->state != MM_TASK_RUNNING);

	if (task->state != MM_TASK_PENDING) {
		mm_runq_put_task(&mm_core->run_queue, task);
		task->state = MM_TASK_PENDING;
	}

	LEAVE();
}

void
mm_sched_start(void)
{
	ENTER();
	ASSERT(mm_running_task == &mm_null_task);

	mm_sched_switch(MM_TASK_BLOCKED);

	LEAVE();
}

void
mm_sched_yield(void)
{
	ENTER();
	ASSERT(mm_running_task != &mm_null_task);

	mm_sched_switch(MM_TASK_PENDING);

	LEAVE();
}

void
mm_sched_block(void)
{
	ENTER();
	ASSERT(mm_running_task != &mm_null_task);

	mm_sched_switch(MM_TASK_BLOCKED);

	LEAVE();
}

void
mm_sched_abort(void)
{
	ENTER();
	ASSERT(mm_running_task != &mm_null_task);

	mm_sched_switch(MM_TASK_INVALID);

	/* Must never get here after the switch above. */
	ABORT();
	LEAVE();
}
