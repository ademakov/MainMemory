/*
 * wait.c - MainMemory wait queues.
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

#include "wait.h"

#include "alloc.h"
#include "core.h"
#include "log.h"
#include "pool.h"
#include "task.h"
#include "timer.h"
#include "trace.h"

// An entry for a waiting task.
struct mm_wait
{
	struct mm_link link;
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

void
mm_wait_cache_prepare(struct mm_wait_cache *cache)
{
	ENTER();

	mm_link_init(&cache->cache);
	cache->cache_size = 0;

	mm_link_init(&cache->pending);

	LEAVE();
}

void
mm_wait_cache_cleanup(struct mm_wait_cache *cache __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static void
mm_wait_cache_put(struct mm_wait_cache *cache, struct mm_wait *wait)
{
	mm_link_insert(&cache->cache, &wait->link);
	cache->cache_size++;
}

static struct mm_wait *
mm_wait_cache_get_low(struct mm_wait_cache *cache)
{
	ASSERT(cache->cache_size > 0);
	ASSERT(!mm_link_empty(&cache->cache));

	struct mm_link *link = mm_link_delete_head(&cache->cache);
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
	mm_link_insert(&cache->pending, &wait->link);
}

void
mm_wait_cache_truncate(struct mm_wait_cache *cache)
{
	ENTER();

	if (!mm_link_empty(&cache->pending)) {
		struct mm_link pending = cache->pending;
		mm_link_init(&cache->pending);

		while (!mm_link_empty(&pending)) {
			struct mm_link *link = mm_link_delete_head(&pending);
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
 * Wait-set initialization and cleanup.
 **********************************************************************/

void
mm_waitset_prepare(struct mm_waitset *waitset)
{
	ENTER();

	mm_link_init(&waitset->set);
	waitset->core = MM_CORE_NONE;

	LEAVE();
}

void
mm_waitset_cleanup(struct mm_waitset *waitset __attribute__((unused)))
{
	ENTER();
	// TODO: ensure the waitset is empty.
	LEAVE();
}

/**********************************************************************
 * Private single-core wait-sets.
 **********************************************************************/

void
mm_waitset_local_wait(struct mm_waitset *waitset)
{
	ENTER();
	ASSERT(waitset->core == mm_core_selfid());

	// Enqueue the task.
	struct mm_wait *wait = mm_wait_cache_get(&mm_core->wait_cache);
	wait->task = mm_task_self();
	mm_link_insert(&waitset->set, &wait->link);

	// Wait for a wakeup signal.
	mm_task_block();

	wait->task = NULL;

	LEAVE();
}

void
mm_waitset_local_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout)
{
	ENTER();
	ASSERT(waitset->core == mm_core_selfid());

	// Enqueue the task.
	struct mm_wait *wait = mm_wait_cache_get(&mm_core->wait_cache);
	wait->task = mm_task_self();
	mm_link_insert(&waitset->set, &wait->link);

	// Wait for a wakeup signal.
	mm_timer_block(timeout);

	wait->task = NULL;

	LEAVE();
}

void
mm_waitset_local_broadcast(struct mm_waitset *waitset)
{
	ENTER();
	ASSERT(waitset->core == mm_core_selfid());

	// Capture the waitset.
	struct mm_link set = waitset->set;
	mm_link_init(&waitset->set);

	while (!mm_link_empty(&set)) {
		// Get the next wait entry.
		struct mm_link *link = mm_link_delete_head(&set);
		struct mm_wait *wait = containerof(link, struct mm_wait, link);
		struct mm_task *task = wait->task;

		if (likely(task != NULL)) {
			// Run the task if it has not been reset.
			wait->task = NULL;
			mm_task_run(task);
		}

		// Return unused wait entry to the pool.
		mm_wait_cache_put(&mm_core->wait_cache, wait);
	}

	LEAVE();
}

/**********************************************************************
 * Shared inter-core wait-sets with locking.
 **********************************************************************/

void
mm_waitset_wait(struct mm_waitset *waitset, mm_task_lock_t *lock)
{
	ENTER();

	// Enqueue the task.
	struct mm_wait *wait = mm_wait_cache_get(&mm_core->wait_cache);
	wait->task = mm_task_self();
	mm_link_insert(&waitset->set, &wait->link);

	// Release the waitset lock.
	mm_task_unlock(lock);

	// Wait for a wakeup signal.
	mm_task_block();

	// Reset the task reference.
	mm_memory_store(wait->task, NULL);

	LEAVE();
}

void
mm_waitset_timedwait(struct mm_waitset *waitset, mm_task_lock_t *lock, mm_timeout_t timeout)
{
	ENTER();

	// Enqueue the task.
	struct mm_wait *wait = mm_wait_cache_get(&mm_core->wait_cache);
	wait->task = mm_task_self();
	mm_link_insert(&waitset->set, &wait->link);

	// Release the waitset lock.
	mm_task_unlock(lock);

	// Wait for a wakeup signal.
	mm_timer_block(timeout);

	// Reset the task reference.
	mm_memory_store(wait->task, NULL);

	LEAVE();
}

void
mm_waitset_broadcast(struct mm_waitset *waitset, mm_task_lock_t *lock)
{
	ENTER();

	// Capture the waitset.
	struct mm_link set = waitset->set;
	mm_link_init(&waitset->set);

	// Release the waitset lock.
	mm_task_unlock(lock);

	while (!mm_link_empty(&set)) {
		// Get the next wait entry.
		struct mm_link *link = mm_link_delete_head(&set);
		struct mm_wait *wait = containerof(link, struct mm_wait, link);
		struct mm_task *task = mm_memory_load(wait->task);

		if (likely(task != NULL)) {
			// Run the task if it has not been reset.
			mm_core_run_task(task);
			// Add used wait entry to the pending list.
			mm_wait_add_pending(&mm_core->wait_cache, wait);
		} else {
			// Return unused wait entry to the pool.
			mm_wait_cache_put(&mm_core->wait_cache, wait);
		}
	}

	LEAVE();
}
