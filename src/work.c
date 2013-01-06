/*
 * work.c - MainMemory work queue.
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

#include "work.h"
#include "util.h"

struct mm_list mm_work_queue;

void
mm_work_init(void)
{
	ENTER();

	mm_list_init(&mm_work_queue);

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
mm_work_create(mm_routine routine, uint32_t count)
{
	ENTER();

	size_t size = sizeof(struct mm_work) + count * sizeof(intptr_t);
	struct mm_work *work = mm_alloc(size);

	work->routine = routine;
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
	if (!mm_list_empty(&mm_work_queue)) {
		struct mm_list *link = mm_list_head(&mm_work_queue);
		work = containerof(link, struct mm_work, queue);
	}

	LEAVE();
	return work;
}

void
mm_work_put(struct mm_work *work)
{
	ENTER();

	mm_list_append(&mm_work_queue, &work->queue);

	LEAVE();
}
