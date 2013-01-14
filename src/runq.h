/*
 * runq.h - MainMemory task run queue.
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

#ifndef RUNQ_H
#define RUNQ_H

#include "common.h"
#include "list.h"

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

void mm_runq_init(struct mm_runq *runq);

struct mm_task *mm_runq_get_task(struct mm_runq *runq);
void mm_runq_put_task(struct mm_runq *runq, struct mm_task *task);
void mm_runq_delete_task(struct mm_runq *runq, struct mm_task *task);

#endif /* RUNQ_H */
