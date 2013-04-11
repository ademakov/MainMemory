/*
 * future.h - MainMemory delayed computation tasks.
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

#ifndef FUTURE_H
#define FUTURE_H

#include "common.h"
#include "list.h"
#include "task.h"
#include "util.h"


typedef enum {
	MM_FUTURE_CREATED,
	MM_FUTURE_STARTED,
	MM_FUTURE_CANCELED,
	MM_FUTURE_COMPLETED,
} mm_future_status_t;


struct mm_future
{
	/* The future status. */
	mm_future_status_t status;
	/* The future result. */
	mm_result_t result;
	/* The future task parameters. */
	mm_task_flags_t flags;
	mm_routine start;
	intptr_t start_arg;
	/* The task if started. */
	struct mm_task *task;
	/* The tasks blocked waiting for the future. */
	struct mm_list blocked_tasks;
};


void mm_future_init(void);
void mm_future_term(void);

struct mm_future * mm_future_create(const char *name, mm_task_flags_t flags,
				    mm_routine start, uintptr_t start_arg);

void mm_future_destroy(struct mm_future *future);

void mm_future_start(struct mm_future *future);

void mm_future_wait(struct mm_future *future);

static inline bool
mm_future_is_canceled(struct mm_future *future)
{
	return future->status == MM_FUTURE_CANCELED;
}

static inline bool
mm_future_is_completed(struct mm_future *future)
{
	return future->status == MM_FUTURE_COMPLETED;
}

#endif /* FUTURE_H */
