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
static void *mm_start_stack_ptr = NULL;

// The currently running task.
struct mm_task *mm_running_task = NULL;

static struct mm_task *
mm_sched_pop_task(void)
{
	ENTER();

	struct mm_task *task = NULL;
	if (likely(!mm_list_empty(&mm_run_queue))) {
		struct mm_list *head = mm_list_head(&mm_run_queue);
		task = containerof(head, struct mm_task, queue);
		mm_list_delete(head);
	}

	LEAVE();
	return task;
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
mm_sched_enqueue(struct mm_task *task)
{
	ENTER();

	mm_list_append(&mm_run_queue, &task->queue);

	LEAVE();
}

void
mm_sched_dequeue(struct mm_task *task)
{
	ENTER();

	mm_list_delete(&task->queue);

	LEAVE();
}

void
mm_sched_start(void)
{
	ENTER();
	ASSERT(mm_running_task == NULL);

	mm_running_task = mm_sched_pop_task();
	if (likely(mm_running_task != NULL)) {
		mm_running_task->state = MM_TASK_RUNNING;
		mm_stack_switch(&mm_start_stack_ptr, &mm_running_task->stack_ptr);
	}

	LEAVE();
}

void
mm_sched_yield(void)
{
	ENTER();
	ASSERT(mm_running_task != NULL);

	struct mm_task *prev_task = mm_running_task;
	if (prev_task->state == MM_TASK_RUNNING) {
		prev_task->state = MM_TASK_PENDING;
		mm_sched_enqueue(prev_task);
	}

	mm_running_task = mm_sched_pop_task();
	if (likely(mm_running_task != NULL)) {
		mm_running_task->state = MM_TASK_RUNNING;
		mm_stack_switch(&prev_task->stack_ptr, &mm_running_task->stack_ptr);
	} else {
		mm_stack_switch(&prev_task->stack_ptr, &mm_start_stack_ptr);
	}

	LEAVE();
}
