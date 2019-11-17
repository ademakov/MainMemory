/*
 * base/fiber/wait.c - MainMemory wait queues.
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

#include "base/fiber/wait.h"

#include "base/logger.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/fiber/fiber.h"
#include "base/fiber/strand.h"
#include "base/memory/pool.h"

// An entry for a waiting fiber.
struct mm_wait
{
	struct mm_slink link;
	struct mm_fiber *fiber;
};

/**********************************************************************
 * Wait entry pool.
 **********************************************************************/

// The memory pool for wait entries.
static struct mm_pool mm_wait_pool;

static void
mm_wait_start(void)
{
	ENTER();

	mm_pool_prepare_shared(&mm_wait_pool, "wait", sizeof(struct mm_wait));

	LEAVE();
}

static void
mm_wait_stop(void)
{
	ENTER();

	mm_pool_cleanup(&mm_wait_pool);

	LEAVE();
}

static struct mm_wait *
mm_wait_create(void)
{
	return mm_pool_alloc(&mm_wait_pool);
}

static void
mm_wait_destroy(struct mm_wait *wait)
{
	mm_pool_free(&mm_wait_pool, wait);
}

/**********************************************************************
 * Wait entry global data initialization and cleanup.
 **********************************************************************/

void
mm_wait_init(void)
{
	ENTER();

	// TODO: switch to common hooks
	mm_regular_start_hook_0(mm_wait_start);
	mm_regular_stop_hook_0(mm_wait_stop);

	LEAVE();
}

/**********************************************************************
 * Per-strand wait entry cache initialization and cleanup.
 **********************************************************************/

#define MM_WAIT_CACHE_MAX	(256)

void NONNULL(1)
mm_wait_cache_prepare(struct mm_wait_cache *cache)
{
	ENTER();

	mm_stack_prepare(&cache->cache);
	cache->cache_size = 0;

	mm_stack_prepare(&cache->pending);

	LEAVE();
}

void NONNULL(1)
mm_wait_cache_cleanup(struct mm_wait_cache *cache UNUSED)
{
	ENTER();
	LEAVE();
}

static void
mm_wait_cache_put(struct mm_wait_cache *cache, struct mm_wait *wait)
{
	mm_stack_insert(&cache->cache, &wait->link);
	cache->cache_size++;
}

static struct mm_wait *
mm_wait_cache_get_low(struct mm_wait_cache *cache)
{
	ASSERT(cache->cache_size > 0);
	ASSERT(!mm_stack_empty(&cache->cache));

	struct mm_slink *link = mm_stack_remove(&cache->cache);
	struct mm_wait *wait = containerof(link, struct mm_wait, link);
	cache->cache_size--;

	return wait;
}

static struct mm_wait *
mm_wait_cache_get(struct mm_wait_cache *cache)
{
	ENTER();

	struct mm_wait *wait;
	if (cache->cache_size > 0) {
		// Reuse a cached wait entry.
		wait = mm_wait_cache_get_low(cache);
	} else {
		// Create a new work item.
		wait = mm_wait_create();
	}

	LEAVE();
	return wait;
}

static void
mm_wait_add_pending(struct mm_wait_cache *cache, struct mm_wait *wait)
{
	mm_stack_insert(&cache->pending, &wait->link);
}

void NONNULL(1)
mm_wait_cache_truncate(struct mm_wait_cache *cache)
{
	ENTER();

	if (!mm_stack_empty(&cache->pending)) {
		struct mm_stack pending = cache->pending;
		mm_stack_prepare(&cache->pending);

		while (!mm_stack_empty(&pending)) {
			struct mm_slink *link = mm_stack_remove(&pending);
			struct mm_wait *wait = containerof(link, struct mm_wait, link);
			struct mm_fiber *fiber = mm_memory_load(wait->fiber);
			if (fiber != NULL) {
				// Add used wait entry to the pending list.
				mm_wait_add_pending(cache, wait);
			} else {
				// Return unused wait entry to the pool.
				mm_wait_cache_put(cache, wait);
			}
		}
	}

	while (cache->cache_size > MM_WAIT_CACHE_MAX) {
		struct mm_wait *wait = mm_wait_cache_get_low(cache);
		mm_wait_destroy(wait);
	}

	LEAVE();
}

/**********************************************************************
 * Shared inter-strand wait-sets with locking.
 **********************************************************************/

void NONNULL(1)
mm_waitset_prepare(struct mm_waitset *waitset)
{
	ENTER();

	mm_stack_prepare(&waitset->set);

	LEAVE();
}

void NONNULL(1, 2)
mm_waitset_wait(struct mm_waitset *waitset, mm_regular_lock_t *lock)
{
	ENTER();

	// Enqueue the fiber.
	struct mm_context *const context = mm_context_selfptr();
	struct mm_strand *const strand = context->strand;
	struct mm_wait *wait = mm_wait_cache_get(&strand->wait_cache);
	wait->fiber = context->fiber;
	mm_stack_insert(&waitset->set, &wait->link);

	// Release the waitset lock.
	mm_regular_unlock(lock);

	// Wait for a wakeup signal.
	mm_fiber_block(context);

	// Reset the fiber reference.
	mm_memory_store(wait->fiber, NULL);

	LEAVE();
}

void NONNULL(1, 2)
mm_waitset_timedwait(struct mm_waitset *waitset, mm_regular_lock_t *lock, mm_timeout_t timeout)
{
	ENTER();

	// Enqueue the fiber.
	struct mm_context *const context = mm_context_selfptr();
	struct mm_strand *const strand = context->strand;
	struct mm_wait *wait = mm_wait_cache_get(&strand->wait_cache);
	wait->fiber = context->fiber;
	mm_stack_insert(&waitset->set, &wait->link);

	// Release the waitset lock.
	mm_regular_unlock(lock);

	// Wait for a wakeup signal.
	mm_fiber_pause(context, timeout);

	// Reset the fiber reference.
	mm_memory_store(wait->fiber, NULL);

	LEAVE();
}

void NONNULL(1, 2)
mm_waitset_broadcast(struct mm_waitset *waitset, mm_regular_lock_t *lock)
{
	ENTER();

	// Capture the waitset.
	struct mm_stack set = waitset->set;
	mm_stack_prepare(&waitset->set);

	// Release the waitset lock.
	mm_regular_unlock(lock);

	while (!mm_stack_empty(&set)) {
		// Get the next wait entry.
		struct mm_slink *link = mm_stack_remove(&set);
		struct mm_wait *wait = containerof(link, struct mm_wait, link);
		struct mm_fiber *fiber = mm_memory_load(wait->fiber);
		struct mm_strand *strand = fiber->strand;

		if (likely(fiber != NULL)) {
			// Run the fiber if it has not been reset.
			mm_strand_run_fiber(fiber);
			// Add used wait entry to the pending list.
			mm_wait_add_pending(&strand->wait_cache, wait);
		} else {
			// Return unused wait entry to the pool.
			mm_wait_cache_put(&strand->wait_cache, wait);
		}
	}

	LEAVE();
}

/**********************************************************************
 * Shared inter-strand wait-set with single waiter fiber.
 **********************************************************************/

void NONNULL(1)
mm_waitset_unique_prepare(struct mm_waitset *waitset)
{
	ENTER();

	waitset->fiber = NULL;

	LEAVE();
}

void NONNULL(1)
mm_waitset_unique_wait(struct mm_waitset *waitset)
{
	ENTER();

	// Advertise the waiting fiber.
	struct mm_context *const context = mm_context_selfptr();
	mm_memory_store(waitset->fiber, context->fiber);
	mm_memory_strict_fence(); // TODO: store_load fence

	if (!mm_memory_load(waitset->signal)) {
		// Wait for a wakeup signal.
		mm_fiber_block(context);
	}

	// Reset the fiber reference.
	mm_memory_store(waitset->signal, false);
	mm_memory_store_fence();
	mm_memory_store(waitset->fiber, NULL);

	LEAVE();
}

void NONNULL(1)
mm_waitset_unique_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout)
{
	ENTER();

	// Advertise the waiting fiber.
	struct mm_context *const context = mm_context_selfptr();
	mm_memory_store(waitset->fiber, context->fiber);
	mm_memory_strict_fence(); // TODO: store_load fence

	if (!mm_memory_load(waitset->signal)) {
		// Wait for a wakeup signal.
		mm_fiber_pause(context, timeout);
	}

	// Reset the fiber reference.
	mm_memory_store(waitset->signal, false);
	mm_memory_store_fence();
	mm_memory_store(waitset->fiber, NULL);

	LEAVE();
}

void NONNULL(1)
mm_waitset_unique_signal(struct mm_waitset *waitset)
{
	ENTER();

	// Note the signal.
	mm_memory_store(waitset->signal, true);
	mm_memory_strict_fence(); // TODO: store_load fence

	// Wake up the waiting fiber if any.
	struct mm_fiber *fiber = mm_memory_load(waitset->fiber);
	if (likely(fiber != NULL))
		mm_strand_run_fiber(fiber);

	LEAVE();
}
