/*
 * base/fiber/future.h - MainMemory delayed computation tasks.
 *
 * Copyright (C) 2013-2019  Aleksey Demakov
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

#ifndef BASE_FIBER_FUTURE_H
#define BASE_FIBER_FUTURE_H

#include "common.h"
#include "base/lock.h"
#include "base/fiber/wait.h"

/* Forward declarations. */
struct mm_strand;

struct mm_future
{
	/* The future task if running. */
	struct mm_fiber *fiber;

	/* The future task parameters. */
	mm_routine_t start;
	mm_value_t start_arg;

	/* The future result. */
	mm_atomic_uintptr_t result;

	/* A cancel request has been made. */
	mm_atomic_uint8_t cancel;

	/* The internal state lock. */
	mm_regular_lock_t lock;

	/* The tasks blocked waiting for the future. */
	struct mm_waitset waitset;
};

/**********************************************************************
 * Futures global data initialization and cleanup.
 **********************************************************************/

void mm_future_init(void);

/**********************************************************************
 * Futures with multiple waiter tasks.
 **********************************************************************/

void NONNULL(1)
mm_future_prepare(struct mm_future *future, mm_routine_t start, mm_value_t start_arg);

void NONNULL(1)
mm_future_cleanup(struct mm_future *future);

struct mm_future * NONNULL(1)
mm_future_create(mm_routine_t start, mm_value_t start_arg);

void NONNULL(1)
mm_future_destroy(struct mm_future *future);

mm_value_t NONNULL(1)
mm_future_start(struct mm_future *future, struct mm_strand *strand);

mm_value_t NONNULL(1)
mm_future_wait(struct mm_future *future);

mm_value_t NONNULL(1)
mm_future_timedwait(struct mm_future *future, mm_timeout_t timeout);

/**********************************************************************
 * Futures with single waiter task.
 **********************************************************************/

void NONNULL(1)
mm_future_unique_prepare(struct mm_future *future, mm_routine_t start, mm_value_t start_arg);

void NONNULL(1)
mm_future_unique_cleanup(struct mm_future *future);

struct mm_future * NONNULL(1)
mm_future_unique_create(mm_routine_t start, mm_value_t start_arg);

void NONNULL(1)
mm_future_unique_destroy(struct mm_future *future);

mm_value_t NONNULL(1)
mm_future_unique_start(struct mm_future *future, struct mm_strand *strand);

mm_value_t NONNULL(1)
mm_future_unique_wait(struct mm_future *future);

mm_value_t NONNULL(1)
mm_future_unique_timedwait(struct mm_future *future, mm_timeout_t timeout);

/**********************************************************************
 * Routines common for any kind of future.
 **********************************************************************/

void NONNULL(1)
mm_future_cancel(struct mm_future *future);

static inline bool NONNULL(1)
mm_future_is_started(struct mm_future *future)
{
	return mm_memory_load(future->result) != MM_RESULT_DEFERRED;
}

static inline bool NONNULL(1)
mm_future_is_canceled(struct mm_future *future)
{
	return mm_memory_load(future->result) == MM_RESULT_CANCELED;
}

static inline bool NONNULL(1)
mm_future_is_finished(struct mm_future *future)
{
	mm_value_t value = mm_memory_load(future->result);
	return value != MM_RESULT_NOTREADY && value != MM_RESULT_DEFERRED;
}

#endif /* BASE_FIBER_FUTURE_H */
