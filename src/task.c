/*
 * task.c - MainMemory tasks.
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

#include "task.h"

#include "pool.h"
#include "port.h"
#include "sched.h"
#include "util.h"

static struct mm_pool mm_task_pool;

void
mm_task_init(void)
{
	ENTER();

	mm_pool_init(&mm_task_pool, sizeof (struct mm_task));

	LEAVE();
}

void
mm_task_term(void)
{
	ENTER();

	mm_pool_discard(&mm_task_pool);

	LEAVE();
}

struct mm_task *
mm_task_create(uint16_t flags, mm_routine start, uintptr_t start_arg)
{
	ENTER();

	struct mm_task *task = mm_pool_alloc(&mm_task_pool);
	task->state = MM_TASK_INVALID;
	task->flags = flags;
	task->blocked_on = NULL;
	task->start = start;
	task->start_arg = start_arg;

	mm_list_init(&task->ports);

	LEAVE();
	return task;
}

void
mm_task_destroy(struct mm_task *task)
{
	ENTER();
	ASSERT(task->state != MM_TASK_BLOCKED);

	if (task->state == MM_TASK_PENDING) {
		mm_sched_dequeue(task);
	}

	while (!mm_list_is_empty(&task->ports)) {
		// TODO: ensure that ports are not referenced from elsewhere.
		struct mm_list *head = mm_list_head(&task->ports);
		struct mm_port *port = containerof(head, struct mm_port, ports);
		mm_port_destroy(port);
	}

	mm_pool_free(&mm_task_pool, task);

	LEAVE();
}

void
mm_task_start(struct mm_task *task)
{
	ENTER();
	ASSERT(task->state == MM_TASK_BLOCKED || task->state == MM_TASK_INVALID);

	mm_sched_enqueue(task);
	task->state = MM_TASK_PENDING;

	LEAVE();
}

void
mm_task_block(struct mm_task *task)
{
	ENTER();

	if (task->state == MM_TASK_PENDING) {
		mm_sched_dequeue(task);
	}

	task->state = MM_TASK_BLOCKED;

	LEAVE();
}
