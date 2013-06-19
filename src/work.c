/*
 * work.c - MainMemory work queue.
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

#include "work.h"

#include "alloc.h"
#include "core.h"
#include "trace.h"

struct mm_work *
mm_work_create(mm_routine_t routine, uintptr_t routine_arg, bool pinned)
{
	ENTER();

	struct mm_work *work;
	if (mm_list_empty(&mm_core->work_cache)) {
		/* Create a new work item. */
		work = mm_alloc(sizeof(struct mm_core));
	} else {
		/* Reuse a cached work item. */
		struct mm_list *link = mm_list_head(&mm_core->work_cache);
		work = containerof(link, struct mm_work, queue);
		mm_list_delete(link);
	}

	work->pinned = pinned;
	work->routine = routine;
	work->routine_arg = routine_arg;

	LEAVE();
	return work;
}

void
mm_work_destroy(struct mm_work *work)
{
	ENTER();

	mm_free(work);

	LEAVE();
}

void
mm_work_recycle(struct mm_work *work)
{
	ENTER();

	mm_list_insert(&mm_core->work_cache, &work->queue);

	LEAVE();
}

struct mm_work *
mm_work_get(void)
{
	ENTER();

	struct mm_work *work;
	if (mm_list_empty(&mm_core->work_queue)) {
		/* No available work items. */
		work = NULL;
	} else {
		/* Take the first available work item. */
		struct mm_list *link = mm_list_head(&mm_core->work_queue);
		work = containerof(link, struct mm_work, queue);
		mm_list_delete(link);
	}

	LEAVE();
	return work;
}

void
mm_work_put(struct mm_work *work)
{
	ENTER();

	/* Queue the work item in the LIFO order. */
	mm_list_insert(&mm_core->work_queue, &work->queue);

	/* If there is a task waiting for work then let it run now. */
	mm_task_signal(&mm_core->wait_queue);

	LEAVE();
}
