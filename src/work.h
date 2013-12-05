/*
 * work.h - MainMemory work items.
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

#ifndef WORK_H
#define WORK_H

#include "common.h"
#include "list.h"

/* A work item. */
struct mm_work
{
	/* A link in the work queue. */
	struct mm_link link;

	/* The work is pinned to a specific core. */
	bool pinned;

	/* The work routine. */
	mm_routine_t routine;
	/* The work routine argument. */
	uintptr_t routine_arg;
};

/* A work queue. */
struct mm_workq
{
	/* The queue of work items. */
	struct mm_queue queue;
	/* The cache of free work items. */
	struct mm_link cache;
	/* The number of items in the work queue. */
	uint32_t queue_size;
	/* The number of items in the free cache. */
	uint32_t cache_size;
};

void mm_work_init(void);
void mm_work_term(void);

void mm_work_prepare(struct mm_workq *queue)
	__attribute__((nonnull(1)));
void mm_work_cleanup(struct mm_workq *queue)
	__attribute__((nonnull(1)));

struct mm_work *mm_work_create(struct mm_workq *queue)
	__attribute__((nonnull(1)));
void mm_work_destroy(struct mm_workq *queue, struct mm_work *work)
	__attribute__((nonnull(1, 2)));

struct mm_work *mm_work_get(struct mm_workq *queue)
	__attribute__((nonnull(1)));
void mm_work_put(struct mm_workq *queue, struct mm_work *work)
	__attribute__((nonnull(1, 2)));

static inline bool
mm_work_available(struct mm_workq *queue)
{
	return queue->queue_size != 0;
}

static inline void
mm_work_set(struct mm_work *work, bool pinned,
	    mm_routine_t routine, uintptr_t routine_arg)
{
	work->pinned = pinned;
	work->routine = routine;
	work->routine_arg = routine_arg;
}

#endif /* WORK_H */
