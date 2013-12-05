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
#include "lock.h"
#include "task.h"
#include "wait.h"

typedef enum {
	MM_FUTURE_CREATED,
	MM_FUTURE_STARTED,
	MM_FUTURE_CANCELED,
	MM_FUTURE_COMPLETED,
} mm_future_status_t;

struct mm_future
{
	/* The internal state lock. */
	mm_core_lock_t lock;

	/* A cancel request has been made. */
	uint8_t cancel;
	/* The future status. */
	uint8_t status;
	/* The future result. */
	mm_result_t result;

	/* The future task parameters. */
	mm_routine_t start;
	intptr_t start_arg;

	/* The task if started. */
	struct mm_task *task;

	/* The tasks blocked waiting for the future. */
	struct mm_waitset waitset;
};

void mm_future_init(void);
void mm_future_term(void);

struct mm_future *mm_future_create(mm_routine_t start, uintptr_t start_arg)
	__attribute__((nonnull(1)));

void mm_future_destroy(struct mm_future *future)
	__attribute__((nonnull(1)));

void mm_future_start(struct mm_future *future, struct mm_core *core)
	__attribute__((nonnull(1)));

void mm_future_cancel(struct mm_future *future)
	__attribute__((nonnull(1)));

mm_result_t mm_future_wait(struct mm_future *future)
	__attribute__((nonnull(1)));

mm_result_t mm_future_timedwait(struct mm_future *future, mm_timeout_t timeout)
	__attribute__((nonnull(1)));

static inline bool
mm_future_is_canceled(struct mm_future *future)
{
	return mm_memory_load(future->status) == MM_FUTURE_CANCELED;
}

static inline bool
mm_future_is_finished(struct mm_future *future)
{
	return mm_memory_load(future->status) >= MM_FUTURE_CANCELED;
}

#endif /* FUTURE_H */
