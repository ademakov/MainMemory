/*
 * base/fiber/runq.h - MainMemory task run queue.
 *
 * Copyright (C) 2013-2017  Aleksey Demakov
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

#ifndef BASE_FIBER_RUNQ_H
#define BASE_FIBER_RUNQ_H

#include "common.h"
#include "base/list.h"

/* Forward declaration. */
struct mm_task;

/* The number of priority bins in the run queue. */
#define MM_RUNQ_BINS 32

/* The run queue is arranged as a priority queue suitable only for
 * a small range of priorities. Specifically the priority range is
 * from 0 to 31. The queue has a separate bin for each priority. */
struct mm_runq
{
	/* The bitmap of non-empty bins. */
	uint32_t bmap;
	/* The bins with elements of the given priority. */
	struct mm_list bins[MM_RUNQ_BINS];
};

void NONNULL(1)
mm_runq_prepare(struct mm_runq *runq);

/* Check to see if there are no pending tasks with given priorities. */
static inline bool NONNULL(1)
mm_runq_empty(struct mm_runq *runq, uint32_t mask)
{
	return (runq->bmap & mask) == 0;
}

/* Check to see if there are no pending tasks with priorities above the given one. */
static inline bool NONNULL(1)
mm_runq_empty_above(struct mm_runq *runq, int prio)
{
	return mm_runq_empty(runq, (1u << prio) - 1);
}

struct mm_task * NONNULL(1)
mm_runq_get(struct mm_runq *runq);

void NONNULL(1, 2)
mm_runq_put(struct mm_runq *runq, struct mm_task *task);

void NONNULL(1, 2)
mm_runq_delete(struct mm_runq *runq, struct mm_task *task);

#endif /* BASE_FIBER_RUNQ_H */
