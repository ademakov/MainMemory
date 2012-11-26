/*
 * sched.c - MainMemory event scheduler.
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

#include "task.h"
#include "util.h"

/* The list of ready to run tasks. */
static struct mm_list mm_run_queue;

/* A dummy task. */
static struct mm_task mm_null_task;

/* The currently running task. */
struct mm_task *mm_running_task;

void
mm_sched_init(void)
{
	ENTER();

	mm_list_init(&mm_run_queue);

	LEAVE();
}

void
mm_sched_free(void)
{
	ENTER();

	LEAVE();
}

void
mm_sched_enqueue(struct mm_task *task)
{
	ENTER();

	mm_list_insert_tail(&mm_run_queue, &task->queue);

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
mm_sched_dispatch(void)
{
	ENTER();

	while (!mm_list_is_empty(&mm_run_queue)) {
		struct mm_list *head = mm_list_head(&mm_run_queue);
		mm_list_delete(head);

		struct mm_task *task = containerof(head, struct mm_task, queue);
		if (task == MM_TASK_PENDING) {
			task->state = MM_TASK_RUNNING;

			mm_running_task = task;
			task->start(task->start_arg);

			if (task->state == MM_TASK_RUNNING) {
				task->state = MM_TASK_PENDING;
				mm_sched_enqueue(task);
			}
		}
	}

	mm_running_task = &mm_null_task;

	LEAVE();
}
