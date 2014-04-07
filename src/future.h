/*
 * future.h - MainMemory delayed computation tasks.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

struct mm_future
{
	/* The future result. */
	mm_atomic_uintptr_t result;

	/* The future task if running. */
	struct mm_task *task;

	/* The future task parameters. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* A cancel request has been made. */
	uint8_t cancel;

	/* The internal state lock. */
	mm_task_lock_t lock;

	/* The tasks blocked waiting for the future. */
	struct mm_waitset waitset;
};

void mm_future_init(void);
void mm_future_term(void);

struct mm_future *mm_future_create(mm_routine_t start, mm_value_t start_arg)
	__attribute__((nonnull(1)));

void mm_future_destroy(struct mm_future *future)
	__attribute__((nonnull(1)));

mm_value_t mm_future_start(struct mm_future *future, mm_core_t core)
	__attribute__((nonnull(1)));

void mm_future_cancel(struct mm_future *future)
	__attribute__((nonnull(1)));

mm_value_t mm_future_wait(struct mm_future *future)
	__attribute__((nonnull(1)));

mm_value_t mm_future_timedwait(struct mm_future *future, mm_timeout_t timeout)
	__attribute__((nonnull(1)));

static inline bool
mm_future_is_started(struct mm_future *future)
{
	return mm_memory_load(future->result.value) != MM_RESULT_DEFERRED;
}

static inline bool
mm_future_is_canceled(struct mm_future *future)
{
	return mm_memory_load(future->result.value) == MM_RESULT_CANCELED;
}

static inline bool
mm_future_is_finished(struct mm_future *future)
{
	mm_value_t value = mm_memory_load(future->result.value);
	return value != MM_RESULT_NOTREADY && value != MM_RESULT_DEFERRED;
}

#endif /* FUTURE_H */
