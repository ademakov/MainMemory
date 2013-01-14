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

#include "core.h"
#include "sched.h"
#include "task.h"
#include "util.h"


static struct mm_list mm_work_queue;
static struct mm_list mm_wait_queue;


void
mm_work_init(void)
{
	ENTER();

	mm_list_init(&mm_work_queue);
	mm_list_init(&mm_wait_queue);

	LEAVE();
}

void
mm_work_term(void)
{
	ENTER();

	while (!mm_list_empty(&mm_work_queue)) {
		struct mm_list *link = mm_list_head(&mm_work_queue);
		struct mm_work *work = containerof(link, struct mm_work, queue);
		mm_work_destroy(work);
	}

	LEAVE();
}

struct mm_work *
mm_work_create(uint32_t count)
{
	ENTER();

	size_t size = sizeof(struct mm_work) + count * sizeof(intptr_t);
	struct mm_work *work = mm_alloc(size);
	work->count = count;

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

struct mm_work *
mm_work_get(void)
{
	ENTER();

	struct mm_work *work = NULL;
	for (;;) {
		/* If there is a work available then take it. */
		if (!mm_list_empty(&mm_work_queue)) {
			struct mm_list *link = mm_list_head(&mm_work_queue);
			work = containerof(link, struct mm_work, queue);
			mm_list_delete(link);
			break;
		}

		/* Otherwise wait for a work to become available. */
		mm_task_wait_lifo(&mm_wait_queue);
	}

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
