/*
 * base/context.h - MainMemory per-thread execution context.
 *
 * Copyright (C) 2019-2020  Aleksey Demakov
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
#include "base/atomic.h"
#include "base/ring.h"
#include "base/task.h"
#include "base/timepiece.h"
#include "base/memory/cache.h"

#define MM_CONTEXT_STATUS	((uint32_t) 3)

typedef enum
{
	MM_CONTEXT_RUNNING = 0,
	MM_CONTEXT_PENDING = 1,
	MM_CONTEXT_POLLING = 2,
	MM_CONTEXT_WAITING = 3,
} mm_context_status_t;

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

	/*
	 * The context status.
	 *
	 * The two least-significant bits contain a mm_context_status_t value.
	 * For MM_CONTEXT_POLLING and MM_CONTEXT_WAITING values the remaining
	 * bits contain a snapshot of the async_queue dequeue stamp. On 32-bit
	 * platforms this discards its 2 most significant bits. However the 30
	 * remaining bits suffice to avoid any stamp clashes in practice.
	 */
	mm_atomic_uintptr_t status;

	/* Associated fiber strand. */
	struct mm_strand *strand;
	/* Associated event listener. */
	struct mm_event_listener *listener;

	/* Fast but coarse clock. */
	struct mm_timepiece clock;

	/* Tasks to execute locally. */
	struct mm_task_list tasks;

	/* The context is waiting for a 'request_tasks' response. */
	bool tasks_request_in_progress;

	/* Asynchronous call queue. */
	struct mm_ring_mpmc async_queue;

	/* Statistics. */
	struct mm_context_stats stats;

	/* Local memory allocator. */
	struct mm_memory_cache cache;
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
 * Time.
 **********************************************************************/

static inline mm_timeval_t NONNULL(1)
mm_context_gettime(struct mm_context *context)
{
	return mm_timepiece_gettime(&context->clock);
}

static inline mm_timeval_t NONNULL(1)
mm_context_getrealtime(struct mm_context *context)
{
	return mm_timepiece_getrealtime(&context->clock);
}

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
mm_context_request_tasks(struct mm_context *self);

void NONNULL(1)
mm_context_distribute_tasks(struct mm_context *self);

#else

#define mm_context_request_tasks(x) ((void) x)
#define mm_context_distribute_tasks(x) ((void) x)

#endif

/**********************************************************************
 * Local memory allocation.
 **********************************************************************/

static inline void * NONNULL(1) MALLOC
mm_context_alloc(struct mm_context *context, size_t size)
{
	return mm_memory_cache_alloc(&context->cache, size);
}

static inline void * NONNULL(1) MALLOC
mm_context_zalloc(struct mm_context *context, size_t size)
{
	return mm_memory_cache_zalloc(&context->cache, size);
}

static inline void * NONNULL(1) MALLOC
mm_context_aligned_alloc(struct mm_context *context, size_t align, size_t size)
{
	return mm_memory_cache_aligned_alloc(&context->cache, align, size);
}

static inline void * NONNULL(1) MALLOC
mm_context_calloc(struct mm_context *context, size_t count, size_t size)
{
	return mm_memory_cache_calloc(&context->cache, count, size);
}

static inline void * NONNULL(1) MALLOC
mm_context_realloc(struct mm_context *context, void *ptr, size_t size)
{
	if (ptr != NULL) {
		return mm_memory_cache_realloc(&context->cache, ptr, size);
	} else {
		return mm_memory_cache_alloc(&context->cache, size);
	}
}

static inline void NONNULL(1)
mm_context_free(struct mm_context *context, void *ptr)
{
	if (ptr != NULL) {
		mm_memory_cache_local_free(&context->cache, ptr);
	}
}

#endif /* BASE_CONTEXT_H */
