/*
 * work.c - MainMemory work items.
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
#include "pool.h"
#include "trace.h"

#define MM_WORK_CACHE_MAX	(256)

// The memory pool for work items.
static struct mm_pool mm_work_pool;

void
mm_work_init(void)
{
	ENTER();

	mm_pool_prepare(&mm_work_pool, "work", &mm_alloc_global,
			sizeof(struct mm_work));

	LEAVE();
}

void
mm_work_term(void)
{
	ENTER();

	mm_pool_cleanup(&mm_work_pool);

	LEAVE();
}

void
mm_work_prepare(struct mm_workq *queue)
{
	ENTER();

	mm_queue_init(&queue->queue);
	mm_link_init(&queue->cache);
	queue->queue_size = 0;
	queue->cache_size = 0;

	LEAVE();
}

void
mm_work_cleanup(struct mm_workq *queue __attribute__((unused)))
{
	ENTER();

#if 0
	while (!mm_queue_empty(&queue->queue)) {
		struct mm_link *link = mm_queue_delete_head(&queue->queue);
		struct mm_work *work = containerof(link, struct mm_work, link);
		mm_pool_free(&mm_work_pool, work);
	}

	while (!mm_link_empty(&queue->cache)) {
		struct mm_link *link = mm_link_delete_head(&queue->cache);
		struct mm_work *work = containerof(link, struct mm_work, link);
		mm_pool_free(&mm_work_pool, work);
	}
#endif

	LEAVE();
}

struct mm_work *
mm_work_create(struct mm_workq *queue)
{
	ENTER();

	struct mm_work *work;
	if (queue->cache_size > 0) {
		// Reuse a cached work item.
		struct mm_link *link = mm_link_delete_head(&queue->cache);
		work = containerof(link, struct mm_work, link);
		queue->cache_size--;
	} else {
		// Create a new work item.
		work = mm_pool_alloc(&mm_work_pool);
	}

	LEAVE();
	return work;
}

void
mm_work_destroy(struct mm_workq *queue, struct mm_work *work)
{
	ENTER();

	if (queue->cache_size < MM_WORK_CACHE_MAX) {
		// Cache the work item.
		mm_link_insert(&queue->cache, &work->link);
		queue->cache_size++;
	} else {
		// Release the work item.
		mm_pool_free(&mm_work_pool, work);
	}

	LEAVE();
}

void
mm_work_put(struct mm_workq *queue, struct mm_work *work)
{
	ENTER();

	mm_queue_append(&queue->queue, &work->link);
	queue->queue_size++;

	LEAVE();
}

struct mm_work *
mm_work_get(struct mm_workq *queue)
{
	ENTER();
	ASSERT(queue->queue_size > 0);

	queue->queue_size--;
	struct mm_link *link = mm_queue_delete_head(&queue->queue);
	struct mm_work *work =  containerof(link, struct mm_work, link);

	LEAVE();
	return work;
}
