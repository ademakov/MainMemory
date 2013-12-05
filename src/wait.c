/*
 * wait.c - MainMemory wait queue.
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

#include "wait.h"

#include "alloc.h"
#include "core.h"
#include "pool.h"
#include "timer.h"
#include "trace.h"

#define MM_WAIT_CACHE_MAX	(256)

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

void
mm_wait_cache_prepare(struct mm_wait_cache *cache)
{
	ENTER();

	mm_link_init(&cache->cache);
	cache->cache_size = 0;

	LEAVE();
}

void
mm_wait_cache_cleanup(struct mm_wait_cache *cache __attribute__((unused)))
{
	ENTER();
	LEAVE();
}

static struct mm_wait *
mm_wait_create(struct mm_wait_cache *cache)
{
	ENTER();

	struct mm_wait *wait;
	if (cache->cache_size > 0) {
		// Reuse a cached wait entry.
		struct mm_link *link = mm_link_delete_head(&cache->cache);
		wait = containerof(link, struct mm_wait, link);
		cache->cache_size--;
	} else {
		// Create a new work item.
		wait = mm_pool_alloc(&mm_wait_pool);
	}

	LEAVE();
	return wait;
}

static void
mm_wait_destroy(struct mm_wait_cache *cache, struct mm_wait *wait)
{
	ENTER();

	if (cache->cache_size < MM_WAIT_CACHE_MAX) {
		// Cache the work item.
		mm_link_insert(&cache->cache, &wait->link);
		cache->cache_size++;
	} else {
		// Release the wait entry.
		mm_pool_free(&mm_wait_pool, wait);
	}

	LEAVE();
}

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
mm_waitset_wait(struct mm_waitset *waitset, mm_core_lock_t *lock)
{
	ENTER();

	// Enqueue the task.
	struct mm_wait *wait = mm_wait_create(&mm_core->wait_cache);
	wait->task = mm_running_task;
	mm_link_insert(&waitset->set, &wait->link);
	waitset->size++;

	// Release the waitset lock.
	mm_core_unlock(lock);

	// Wait for a wakeup signal.
	mm_task_block();

	// Reset the task reference.
	mm_memory_store(wait->task, NULL);

	LEAVE();
}

void
mm_waitset_timedwait(struct mm_waitset *waitset, mm_core_lock_t *lock, mm_timeout_t timeout)
{
	ENTER();

	// Enqueue the task.
	struct mm_wait *wait = mm_wait_create(&mm_core->wait_cache);
	wait->task = mm_running_task;
	mm_link_insert(&waitset->set, &wait->link);
	waitset->size++;

	// Release the waitset lock.
	mm_core_unlock(lock);

	// Wait for a wakeup signal.
	mm_timer_block(timeout);

	// Reset the task reference.
	mm_memory_store(wait->task, NULL);

	LEAVE();
}

void
mm_waitset_broadcast(struct mm_waitset *waitset, mm_core_lock_t *lock)
{
	ENTER();

	// Capture the waitset.
	struct mm_link set = waitset->set;
	mm_link_init(&waitset->set);
	waitset->size = 0;

	// Release the waitset lock.
	mm_core_unlock(lock);

	while (!mm_link_empty(&set)) {
		// Get the next wait entry.
		struct mm_link *link = mm_link_delete_head(&set);
		struct mm_wait *wait = containerof(link, struct mm_wait, link);
		struct mm_task *task = mm_memory_load(wait->task);

		// Free the entry.
		mm_wait_destroy(&mm_core->wait_cache, wait);

		// Run the task if it has not been reset.
		if (likely(task != NULL)) {
			mm_core_run_task(task);
		}
	}

	LEAVE();
}
