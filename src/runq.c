/*
 * runq.c - MainMemory task run queue.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#include "runq.h"
#include "util.h"
#include "task.h"

void
mm_runq_init(struct mm_runq *runq)
{
	ASSERT(MM_RUNQ_BINS <= (8 * sizeof(runq->bmap)));

	runq->bmap = 0;
	for (int i = 0; i < MM_RUNQ_BINS; i++) {
		mm_list_init(&runq->bins[i]);
	}
}

void
mm_runq_add_task(struct mm_runq *runq, struct mm_task *task)
{
	int priority = task->priority;
	ASSERT(priority >= 0 && priority < MM_RUNQ_BINS);

	runq->bmap |= (1 << priority);
	mm_list_append(&runq->bins[priority], &task->queue);
}

void
mm_runq_delete_task(struct mm_runq *runq, struct mm_task *task)
{
	int priority = task->priority;
	ASSERT(priority >= 0 && priority < MM_RUNQ_BINS);

	mm_list_delete(&task->queue);
	if (mm_list_empty(&runq->bins[priority]))
		runq->bmap &= ~(1 << priority);
}

struct mm_task *
mm_runq_get_task(struct mm_runq *runq)
{
	if (unlikely(!runq->bmap))
		return NULL;

	int priority = ctz(runq->bmap);
	ASSERT(priority >= 0 && priority < MM_RUNQ_BINS);
	ASSERT(!mm_list_empty(&runq->bins[priority]));

	struct mm_list *head = mm_list_head(&runq->bins[priority]);
	struct mm_task *task = containerof(head, struct mm_task, queue);
	ASSERT(priority == task->priority);

	mm_list_delete(head);
	if (mm_list_empty(&runq->bins[priority]))
		runq->bmap &= ~(1 << priority);

	return task;
}