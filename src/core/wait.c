/*
 * core/wait.c - MainMemory wait queues.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#include "core/wait.h"
#include "core/core.h"
#include "core/task.h"
#include "core/timer.h"

#include "base/log/log.h"
#include "base/log/trace.h"
#include "base/memory/pool.h"

// An entry for a waiting task.
struct mm_wait
{
	struct mm_slink link;
	struct mm_task *task;
};

/**********************************************************************
 * Wait entry pool.
 **********************************************************************/

// The memory pool for waiting tasks.
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

	mm_core_hook_start(mm_wait_start);
	mm_core_hook_stop(mm_wait_stop);

	LEAVE();
}

void
mm_wait_term(void)
{
	ENTER();

	LEAVE();
}

/**********************************************************************
 * Per-core wait entry cache initialization and cleanup.
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
			struct mm_task *task = mm_memory_load(wait->task);
			if (task != NULL) {
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
 * Shared inter-core wait-sets with locking.
 **********************************************************************/

void NONNULL(1)
mm_waitset_prepare(struct mm_waitset *waitset)
{
	ENTER();

	mm_stack_prepare(&waitset->set);
	waitset->core = MM_CORE_NONE;

	LEAVE();
}

void NONNULL(1, 2)
mm_waitset_wait(struct mm_waitset *waitset, mm_regular_lock_t *lock)
{
	ENTER();

	// Enqueue the task.
	struct mm_core *core = mm_core_selfptr();
	struct mm_wait *wait = mm_wait_cache_get(&core->wait_cache);
	wait->task = mm_task_selfptr();
	mm_stack_insert(&waitset->set, &wait->link);

	// Release the waitset lock.
	mm_regular_unlock(lock);

	// Wait for a wakeup signal.
	mm_task_block();

	// Reset the task reference.
	mm_memory_store(wait->task, NULL);

	LEAVE();
}

void NONNULL(1, 2)
mm_waitset_timedwait(struct mm_waitset *waitset, mm_regular_lock_t *lock, mm_timeout_t timeout)
{
	ENTER();

	// Enqueue the task.
	struct mm_core *core = mm_core_selfptr();
	struct mm_wait *wait = mm_wait_cache_get(&core->wait_cache);
	wait->task = mm_task_selfptr();
	mm_stack_insert(&waitset->set, &wait->link);

	// Release the waitset lock.
	mm_regular_unlock(lock);

	// Wait for a wakeup signal.
	mm_timer_block(timeout);

	// Reset the task reference.
	mm_memory_store(wait->task, NULL);

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
		struct mm_task *task = mm_memory_load(wait->task);
		struct mm_core *core = mm_core_selfptr();

		if (likely(task != NULL)) {
			// Run the task if it has not been reset.
			mm_core_run_task(task);
			// Add used wait entry to the pending list.
			mm_wait_add_pending(&core->wait_cache, wait);
		} else {
			// Return unused wait entry to the pool.
			mm_wait_cache_put(&core->wait_cache, wait);
		}
	}

	LEAVE();
}

/**********************************************************************
 * Private single-core wait-sets.
 **********************************************************************/

void NONNULL(1)
mm_waitset_local_prepare(struct mm_waitset *waitset, mm_core_t core)
{
	ENTER();
	ASSERT(core != MM_CORE_NONE && core != MM_CORE_SELF);

	mm_stack_prepare(&waitset->set);
	waitset->core = core;

	LEAVE();
}

void NONNULL(1)
mm_waitset_local_wait(struct mm_waitset *waitset)
{
	ENTER();
	ASSERT(waitset->core == mm_core_self());

	// Enqueue the task.
	struct mm_core *core = mm_core_selfptr();
	struct mm_wait *wait = mm_wait_cache_get(&core->wait_cache);
	wait->task = mm_task_selfptr();
	mm_stack_insert(&waitset->set, &wait->link);

	// Wait for a wakeup signal.
	mm_task_block();

	wait->task = NULL;

	LEAVE();
}

void NONNULL(1)
mm_waitset_local_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout)
{
	ENTER();
	ASSERT(waitset->core == mm_core_self());

	// Enqueue the task.
	struct mm_core *core = mm_core_selfptr();
	struct mm_wait *wait = mm_wait_cache_get(&core->wait_cache);
	wait->task = mm_task_selfptr();
	mm_stack_insert(&waitset->set, &wait->link);

	// Wait for a wakeup signal.
	mm_timer_block(timeout);

	wait->task = NULL;

	LEAVE();
}

void NONNULL(1)
mm_waitset_local_broadcast(struct mm_waitset *waitset)
{
	ENTER();
	ASSERT(waitset->core == mm_core_self());

	// Capture the waitset.
	struct mm_stack set = waitset->set;
	mm_stack_prepare(&waitset->set);

	while (!mm_stack_empty(&set)) {
		// Get the next wait entry.
		struct mm_slink *link = mm_stack_remove(&set);
		struct mm_wait *wait = containerof(link, struct mm_wait, link);
		struct mm_task *task = wait->task;

		if (likely(task != NULL)) {
			// Run the task if it has not been reset.
			wait->task = NULL;
			mm_task_run(task);
		}

		// Return unused wait entry to the pool.
		struct mm_core *core = mm_core_selfptr();
		mm_wait_cache_put(&core->wait_cache, wait);
	}

	LEAVE();
}

/**********************************************************************
 * Shared inter-core wait-set with single waiter task.
 **********************************************************************/

void NONNULL(1)
mm_waitset_unique_prepare(struct mm_waitset *waitset)
{
	ENTER();

	waitset->task = NULL;
	waitset->core = MM_CORE_SELF;

	LEAVE();
}

void NONNULL(1)
mm_waitset_unique_wait(struct mm_waitset *waitset)
{
	ENTER();

	// Advertise the waiting task.
	mm_memory_store(waitset->task, mm_task_selfptr());
	mm_memory_strict_fence(); // TODO: store_load fence

	if (!mm_memory_load(waitset->signal)) {
		// Wait for a wakeup signal.
		mm_task_block();
	}

	// Reset the task reference.
	mm_memory_store(waitset->signal, false);
	mm_memory_store_fence();
	mm_memory_store(waitset->task, NULL);

	LEAVE();
}

void NONNULL(1)
mm_waitset_unique_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout)
{
	ENTER();

	// Advertise the waiting task.
	mm_memory_store(waitset->task, mm_task_selfptr());
	mm_memory_strict_fence(); // TODO: store_load fence

	if (!mm_memory_load(waitset->signal)) {
		// Wait for a wakeup signal.
		mm_timer_block(timeout);
	}

	// Reset the task reference.
	mm_memory_store(waitset->signal, false);
	mm_memory_store_fence();
	mm_memory_store(waitset->task, NULL);

	LEAVE();
}

void NONNULL(1)
mm_waitset_unique_signal(struct mm_waitset *waitset)
{
	ENTER();

	// Note the signal.
	mm_memory_store(waitset->signal, true);
	mm_memory_strict_fence(); // TODO: store_load fence

	// Wake up the waiting task, if any.
	struct mm_task *task = mm_memory_load(waitset->task);
	if (likely(task != NULL))
		mm_core_run_task(task);

	LEAVE();
}
