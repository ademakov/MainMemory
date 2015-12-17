/*
 * core/runq.c - MainMemory task run queue.
 *
 * Copyright (C) 2013,2015  Aleksey Demakov
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

#include "core/runq.h"

#include "core/task.h"
#include "base/bitops.h"
#include "base/log/trace.h"

void NONNULL(1)
mm_runq_prepare(struct mm_runq *runq)
{
	ENTER();
	ASSERT(MM_RUNQ_BINS <= (8 * sizeof(runq->bmap)));

	runq->bmap = 0;
	for (int i = 0; i < MM_RUNQ_BINS; i++) {
		mm_list_prepare(&runq->bins[i]);
	}

	LEAVE();
}

struct mm_task * NONNULL(1)
mm_runq_get(struct mm_runq *runq)
{
	ASSERT(runq->bmap != 0);

	int priority = mm_ctz(runq->bmap);
	ASSERT(priority >= 0 && priority < MM_RUNQ_BINS);
	ASSERT(!mm_list_empty(&runq->bins[priority]));

	struct mm_link *link = mm_list_remove_head(&runq->bins[priority]);
	struct mm_task *task = task = containerof(link, struct mm_task, queue);
	if (mm_list_empty(&runq->bins[priority]))
		runq->bmap &= ~(1 << priority);
	ASSERT(priority == task->priority);

	return task;
}

void NONNULL(1, 2)
mm_runq_put(struct mm_runq *runq, struct mm_task *task)
{
	int priority = task->priority;
	ASSERT(priority >= 0 && priority < MM_RUNQ_BINS);

	runq->bmap |= (1 << priority);
	mm_list_append(&runq->bins[priority], &task->queue);
}

void NONNULL(1, 2)
mm_runq_delete(struct mm_runq *runq, struct mm_task *task)
{
	int priority = task->priority;
	ASSERT(priority >= 0 && priority < MM_RUNQ_BINS);
	ASSERT(!mm_list_empty(&runq->bins[priority]));

	mm_list_delete(&task->queue);
	if (mm_list_empty(&runq->bins[priority]))
		runq->bmap &= ~(1 << priority);
}
