/*
 * wait.c - MainMemory wait queue.
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
#include "timer.h"
#include "trace.h"

/**********************************************************************
 * Wait entries.
 **********************************************************************/

// The memory pool for waiting tasks.
static struct mm_pool mm_wait_pool;

void
mm_wait_init(void)
{
	ENTER();

	mm_pool_prepare(&mm_wait_pool, "wait", &mm_alloc_global,
			sizeof(struct mm_wait));

	LEAVE();
}

void
mm_wait_term(void)
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
 * Cache of wait entries.
 **********************************************************************/

#define MM_WAIT_CACHE_MAX	(256)

void
mm_wait_cache_prepare(struct mm_wait_cache *cache)
{
	ENTER();

	mm_link_init(&cache->cache);
	cache->cache_size = 0;

	mm_link_init(&cache->pending);
	cache->pending_count = 0;

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
	cache->pending_count++;
}

void
mm_wait_cache_truncate(struct mm_wait_cache *cache)
{
	ENTER();

	if (cache->pending_count) {
		struct mm_link pending = cache->pending;
		mm_link_init(&cache->pending);
		cache->pending_count = 0;

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
 * Wait-set functions.
 **********************************************************************/

void
mm_waitset_prepare(struct mm_waitset *waitset)
{
	ENTER();

	mm_link_init(&waitset->set);
	waitset->size = 0;

	LEAVE();
}

void
mm_waitset_cleanup(struct mm_waitset *waitset __attribute__((unused)))
{
	ENTER();
	// TODO: ensure the waitset is empty.
	ASSERT(waitset->size == 0);
	LEAVE();
}

void
mm_waitset_wait(struct mm_waitset *waitset, mm_task_lock_t *lock)
{
	ENTER();

	// Enqueue the task.
	struct mm_wait *wait = mm_wait_cache_get(&mm_core->wait_cache);
	wait->task = mm_running_task;
	mm_link_insert(&waitset->set, &wait->link);
	waitset->size++;

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
	wait->task = mm_running_task;
	mm_link_insert(&waitset->set, &wait->link);
	waitset->size++;

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
	waitset->size = 0;

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
