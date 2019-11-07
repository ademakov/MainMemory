/*
 * base/context.h - MainMemory per-thread execution context.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#ifndef BASE_CONTEXT_H
#define BASE_CONTEXT_H

#include "common.h"
#include "base/ring.h"
#include "base/task.h"
#include "base/timesource.h"

struct mm_context_stats
{
	/* Asynchronous call statistics. */
	uint64_t enqueued_async_calls;
	uint64_t enqueued_async_posts;
	uint64_t dequeued_async_calls;
	uint64_t direct_calls;
};

struct mm_context
{
	/* Currently running fiber. */
	struct mm_fiber *fiber;

	/* Associated fiber strand. */
	struct mm_strand *strand;
	/* Associated event listener. */
	struct mm_event_listener *listener;

	/* Fast but coarse clock. */
	struct mm_timesource timesource;

	/* Tasks to execute locally. */
	struct mm_task_list tasks;

	/* Asynchronous call queue. */
	struct mm_ring_mpmc *async_queue;

	/* Statistics. */
	struct mm_context_stats stats;
};

extern __thread struct mm_context *__mm_context_self;

static inline struct mm_context *
mm_context_selfptr(void)
{
	return __mm_context_self;
}

static inline struct mm_strand *
mm_context_strand(void)
{
	return mm_context_selfptr()->strand;
}

static inline struct mm_event_listener *
mm_context_listener(void)
{
	return mm_context_selfptr()->listener;
}

void NONNULL(1)
mm_context_prepare(struct mm_context *context, mm_thread_t ident, uint32_t async_queue_size);

void NONNULL(1)
mm_context_cleanup(struct mm_context *context);

void NONNULL(1)
mm_context_report_stats(struct mm_context_stats *stats);

/**********************************************************************
 * Asynchronous task scheduling.
 **********************************************************************/

void NONNULL(1, 2)
mm_context_add_task(struct mm_context *self, mm_task_t task, mm_value_t arg);

void NONNULL(1, 2)
mm_context_send_task(struct mm_context *peer, mm_task_t task, mm_value_t arg);

void NONNULL(1)
mm_context_post_task(mm_task_t task, mm_value_t arg);

#if ENABLE_SMP
void NONNULL(1)
mm_context_distribute_tasks(struct mm_context *self);
#endif

#endif /* BASE_CONTEXT_H */
