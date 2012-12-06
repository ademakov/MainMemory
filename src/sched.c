/*
 * sched.c - MainMemory task scheduler.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "sched.h"

#include "arch.h"
#include "task.h"
#include "util.h"

// The list of ready to run tasks.
static struct mm_list mm_run_queue;

// The original stack pointer.
static struct mm_task mm_null_task = {
	.state = MM_TASK_RUNNING,
	.name = "null-task",
};

// The currently running task.
__thread struct mm_task *mm_running_task = &mm_null_task;

static struct mm_task *
mm_sched_pop_task(void)
{
	if (unlikely(mm_list_empty(&mm_run_queue))) {
		return &mm_null_task;
	} else {
		struct mm_list *head = mm_list_head(&mm_run_queue);
		struct mm_task *task = containerof(head, struct mm_task, queue);
		mm_list_delete(head);
		return task;
	}
}

void
mm_sched_init(void)
{
	ENTER();

	mm_list_init(&mm_run_queue);

	LEAVE();
}

void
mm_sched_term(void)
{
	ENTER();

	LEAVE();
}

void
mm_sched_run(struct mm_task *task)
{
	ENTER();
	ASSERT(task->state == MM_TASK_BLOCKED || task->state == MM_TASK_CREATED);

	mm_list_append(&mm_run_queue, &task->queue);
	task->state = MM_TASK_PENDING;

	LEAVE();
}

void
mm_sched_start(void)
{
	ENTER();
	ASSERT(mm_running_task == &mm_null_task);

	mm_running_task = mm_sched_pop_task();
	if (likely(mm_running_task != &mm_null_task)) {
		mm_running_task->state = MM_TASK_RUNNING;
		mm_stack_switch(&mm_null_task.stack_ctx,
				&mm_running_task->stack_ctx);
	}

	LEAVE();
}

void
mm_sched_yield(void)
{
	ENTER();
	ASSERT(mm_running_task != &mm_null_task);

	struct mm_task *task = mm_running_task;
	if (task->state == MM_TASK_RUNNING) {
		task->state = MM_TASK_PENDING;
		mm_list_append(&mm_run_queue, &task->queue);
	}

	mm_running_task = mm_sched_pop_task();
	mm_running_task->state = MM_TASK_RUNNING;
	mm_stack_switch(&task->stack_ctx,
			&mm_running_task->stack_ctx);

	LEAVE();
}

void
mm_sched_block(void)
{
	ENTER();
	ASSERT(mm_running_task != &mm_null_task);
	ASSERT(mm_running_task->state == MM_TASK_RUNNING);

	mm_running_task->state = MM_TASK_BLOCKED;
	mm_sched_yield();

	LEAVE();
}
