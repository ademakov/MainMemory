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
#include "pool.h"
#include "sched.h"
#include "task.h"
#include "trace.h"


static struct mm_pool mm_work_pool;
static struct mm_list mm_work_queue;
static struct mm_list mm_wait_queue;


void
mm_work_init(void)
{
	ENTER();

	mm_pool_prepare(&mm_work_pool, "work",
			&mm_alloc_global, sizeof(struct mm_work));

	mm_list_init(&mm_work_queue);
	mm_list_init(&mm_wait_queue);

	LEAVE();
}

void
mm_work_term(void)
{
	ENTER();

	mm_pool_cleanup(&mm_work_pool);

	LEAVE();
}

struct mm_work *
mm_work_create(mm_task_flags_t flags, mm_routine_t routine, uintptr_t item)
{
	ENTER();

	struct mm_work *work = mm_pool_alloc(&mm_work_pool);
	work->flags = flags;
	work->routine = routine;
	work->item = item;

	LEAVE();
	return work;
}

void
mm_work_destroy(struct mm_work *work)
{
	ENTER();

	mm_pool_free(&mm_work_pool, work);

	LEAVE();
}

struct mm_work *
mm_work_get(void)
{
	ENTER();

	/* Wait for a work to become available. */
	while (mm_list_empty(&mm_work_queue)) {
		mm_task_wait_lifo(&mm_wait_queue);
 		mm_task_testcancel();
	}

	/* Take the first available work. */
	struct mm_list *link = mm_list_head(&mm_work_queue);
	struct mm_work *work = containerof(link, struct mm_work, queue);
	mm_list_delete(link);

	LEAVE();
	return work;
}

void
mm_work_put(struct mm_work *work)
{
	ENTER();

	/* Add the work to the stack. */
	mm_list_insert(&mm_work_queue, &work->queue);

	/* If there is a task waiting for a work then let it run now. */
	mm_task_signal(&mm_wait_queue);

	LEAVE();
}

void
mm_work_add(mm_task_flags_t flags, mm_routine_t routine, uintptr_t item)
{
	ENTER();
	
	struct mm_work *work = mm_work_create(flags, routine, item);
	mm_work_put(work);

	LEAVE();
}

void
mm_work_addv(mm_task_flags_t flags, mm_routine_t routine, uintptr_t *items, size_t nitems)
{
	ENTER();

	for (size_t i = 0; i < nitems; i++) {
		struct mm_work *work = mm_work_create(flags, routine, items[i]);
		mm_work_put(work);
	}

	LEAVE();
}
