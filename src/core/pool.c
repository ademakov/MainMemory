/*
 * core/pool.c - MainMemory memory pools.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#include "core/pool.h"
#include "core/core.h"

#include "base/log/error.h"
#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/mem/alloc.h"
#include "base/thread/domain.h"
#include "base/util/format.h"

#define MM_POOL_BLOCK_SIZE	(0x2000)

/**********************************************************************
 * Generic pool routines.
 **********************************************************************/

static void
mm_pool_grow_lock(struct mm_pool *pool)
{
	if (pool->global)
		mm_thread_lock(&pool->global_data.grow_lock);
#if ENABLE_SMP
	else if (pool->shared)
		mm_task_lock(&pool->shared_data.grow_lock);
#endif
}

static void
mm_pool_grow_unlock(struct mm_pool *pool)
{
	if (pool->global)
		mm_thread_unlock(&pool->global_data.grow_lock);
#if ENABLE_SMP
	else if (pool->shared)
		mm_task_unlock(&pool->shared_data.grow_lock);
#endif
}

static void
mm_pool_grow(struct mm_pool *pool)
{
	ENTER();

	// Check for 32-bit integer overflow.
	uint32_t total_capacity = pool->block_capacity * pool->block_array_used;
	if (unlikely(total_capacity > (total_capacity + pool->block_capacity)))
		mm_fatal(0, "the '%s' memory pool overflow", pool->pool_name);

	// If needed grow the block container array.
	if (pool->block_array_used == pool->block_array_size) {
		if (pool->block_array_size)
			pool->block_array_size *= 2;
		else
			pool->block_array_size = 4;

		pool->block_array = mm_arena_realloc(
			pool->arena,
			pool->block_array,
			pool->block_array_size * sizeof(char *));
	}

	// Allocate a new memory block.
	char *block = mm_arena_alloc(pool->arena, MM_POOL_BLOCK_SIZE);
	pool->block_array[pool->block_array_used] = block;
	pool->block_array_used++;

	pool->block_cur_ptr = block;
	pool->block_end_ptr = block + pool->block_capacity * pool->item_size;

	mm_verbose("grow the '%s' memory pool to %u elements, occupy %lu bytes",
		   pool->pool_name,
		   pool->block_capacity * pool->block_array_used,
		   (unsigned long) MM_POOL_BLOCK_SIZE * pool->block_array_used);

	LEAVE();
}

static void *
mm_pool_alloc_new(struct mm_pool *pool)
{
	if (unlikely(pool->block_cur_ptr == pool->block_end_ptr))
		mm_pool_grow(pool);

	void *item = pool->block_cur_ptr;
	pool->block_cur_ptr += pool->item_size;
	pool->item_last++;

	return item;
}

static void
mm_pool_prepare_low(struct mm_pool *pool,
		    const char *pool_name,
		    const struct mm_arena *arena,
		    uint32_t item_size)
{
	ASSERT(item_size < 0x200);

	if (item_size < sizeof(struct mm_link))
		item_size = sizeof(struct mm_link);

	mm_verbose("make the '%s' memory pool with element size %u",
		   pool_name, item_size);

	pool->item_last = 0;
	pool->item_size = item_size;

	pool->block_capacity = MM_POOL_BLOCK_SIZE / item_size;
	pool->block_array_used = 0;
	pool->block_array_size = 0;

	pool->arena = arena;
	pool->block_array = NULL;
	pool->block_cur_ptr = NULL;
	pool->block_end_ptr = NULL;

	mm_link_init(&pool->free_list);

	pool->pool_name = mm_global_strdup(pool_name);
}

void
mm_pool_cleanup(struct mm_pool *pool)
{
	ENTER();

	for (uint32_t i = 0; i < pool->block_array_used; i++)
		mm_arena_free(pool->arena, pool->block_array[i]);
	mm_arena_free(pool->arena, pool->block_array);

	mm_global_free(pool->pool_name);

	LEAVE();
}

void *
mm_pool_idx2ptr(struct mm_pool *pool, uint32_t item_idx)
{
	uint32_t block = item_idx / pool->block_capacity;
	uint32_t index = item_idx % pool->block_capacity;

	mm_pool_grow_lock(pool);

	void *item_ptr;
	if (unlikely(item_idx >= pool->item_last))
		item_ptr = NULL;
	else
		item_ptr = pool->block_array[block] + index * pool->item_size;

	mm_pool_grow_unlock(pool);

	return item_ptr;
}

uint32_t
mm_pool_ptr2idx(struct mm_pool *pool, const void *item_ptr)
{
	char *start = NULL;
	uint32_t block = 0;

	mm_pool_grow_lock(pool);

	while (block < pool->block_array_used) {
		char *s_ptr = pool->block_array[block];
		char *e_ptr = s_ptr + MM_POOL_BLOCK_SIZE;
		if((char *) item_ptr >= s_ptr && (char *) item_ptr < e_ptr) {
			start = s_ptr;
			break;
		}
		++block;
	}

	mm_pool_grow_unlock(pool);

	if (unlikely(start == NULL))
		return MM_POOL_INDEX_INVALID;

	uint32_t index = ((char *) item_ptr - start) / pool->item_size;

	return block * pool->block_capacity + index;
}

bool
mm_pool_contains(struct mm_pool *pool, const void *item)
{
	bool rc = false;
	uint32_t block = 0;

	mm_pool_grow_lock(pool);

	while (block < pool->block_array_used) {
		char *s_ptr = pool->block_array[block];
		char *e_ptr = s_ptr + MM_POOL_BLOCK_SIZE;
		if((char *) item >= s_ptr && (char *) item < e_ptr) {
			rc = true;
			break;
		}
		++block;
	}

	mm_pool_grow_unlock(pool);

	return rc;
}

/**********************************************************************
 * Private single-core pools.
 **********************************************************************/

void *
mm_pool_local_alloc(struct mm_pool *pool)
{
	ENTER();
	void *item;

	if (!mm_link_empty(&pool->free_list))
		item = mm_link_delete_head(&pool->free_list);
	else
		item = mm_pool_alloc_new(pool);

	LEAVE();
	return item;
}

void
mm_pool_local_free(struct mm_pool *pool, void *item)
{
	ENTER();
	ASSERT(mm_pool_contains(pool, item));

	mm_link_insert(&pool->free_list, (struct mm_link *) item);

	LEAVE();
}

void
mm_pool_prepare(struct mm_pool *pool, const char *name,
		const struct mm_arena *arena, uint32_t item_size)
{
	ENTER();

	mm_pool_prepare_low(pool, name, arena, item_size);

	pool->shared = false;
	pool->global = false;

	pool->alloc_item = mm_pool_local_alloc;
	pool->free_item = mm_pool_local_free;

	LEAVE();
}

/**********************************************************************
 * Shared pools.
 **********************************************************************/

#define MM_POOL_FREE_BATCH	(16)
#define MM_POOL_FREE_THRESHOLD	(32)

#if ENABLE_SMP

struct mm_pool_shared_cdata
{
	/* Free items cache. */
	struct mm_link cache;

	/* ABA-problem guard. */
	struct mm_link *item_guard;
	struct mm_link **guard_buffer;

	/* Number of items in the cache. */
	uint32_t cache_size;

	/* The cache is full. */
	bool cache_full;
};

void *
mm_pool_shared_alloc_low(mm_core_t core, struct mm_pool *pool)
{
	ENTER();
	ASSERT(pool->shared);
	void *item;

	struct mm_pool_shared_cdata *cdata =
		MM_CDATA_DEREF(core, pool->shared_data.cdata);

	if (!mm_link_empty(&cdata->cache)) {
		// Get an item from the core-local cache.
		item = mm_link_delete_head(&cdata->cache);
		cdata->cache_size--;

	} else {
		// Get an item form the shared free list.
		struct mm_link *head = mm_link_head(&pool->free_list);
		if (head != NULL) {
			for (uint32_t b = 0; ; b = mm_backoff(b)) {
				// Prevent ABA-problem.
				mm_memory_store(cdata->item_guard, head);

				// Make sure the guard store is not reordered
				// wrt 'head->next' load below.
				mm_memory_strict_fence();

				// Try to pop the item atomically.
				struct mm_link *old_head = head;
				head = mm_link_cas_head(&pool->free_list, head, head->next);
				if (head == old_head || head == NULL)
					break;
  			}

			cdata->item_guard = NULL;
		}

		if (head != NULL) {
			item = head;
		} else {
			// Allocate a new item.
			mm_task_lock(&pool->shared_data.grow_lock);
			item = mm_pool_alloc_new(pool);
			mm_task_unlock(&pool->shared_data.grow_lock);
		}
	}

	LEAVE();
	return item;
}

void
mm_pool_shared_free_low(mm_core_t core, struct mm_pool *pool, void *item)
{
	ENTER();
	ASSERT(pool->shared);
	ASSERT(mm_pool_contains(pool, item));

	struct mm_pool_shared_cdata *cdata =
		MM_CDATA_DEREF(core, pool->shared_data.cdata);

	// Find out if the core-local cache is too large.
	if (cdata->cache_size < MM_POOL_FREE_THRESHOLD) {
		cdata->cache_full = false;
	} else {
		uint32_t aver = pool->item_last / mm_core_getnum();
		if (cdata->cache_full) {
			if (cdata->cache_size < (aver - aver / 8))
				cdata->cache_full = false;
		} else {
			if (cdata->cache_size > (aver + aver / 8))
				cdata->cache_full = true;
		}
	}

	// Add the item to the core-local cache.
	mm_link_insert(&cdata->cache, (struct mm_link *) item);
	cdata->cache_size++;

	// If the core-local cache is too large move some number of items
	// from it to the shared free list.
	if (cdata->cache_full) {
		// Collect items that might be subjects to ABA-problem.
		int nguards = 0;
		struct mm_link **guards = cdata->guard_buffer;
		mm_core_t n = mm_core_getnum();
		for (mm_core_t i = 0; i < n; i++) {
			struct mm_pool_shared_cdata *cd =
				MM_CDATA_DEREF(i, pool->shared_data.cdata);
			struct mm_link *guard = mm_memory_load(cd->item_guard);
			if (guard != NULL)
				guards[nguards++] = guard;
		}

		// Collect the items to move.
		int nitems = 0;
		struct mm_link *head = NULL;
		struct mm_link *tail = NULL;
		struct mm_link *prev = &cdata->cache;
		while (nitems < MM_POOL_FREE_BATCH) {
			struct mm_link *link = prev->next;
			if (link == NULL)
				break;

			bool guarded = false;
			for (int i = 0; i < nguards; i++) {
				if (link == guards[i]) {
					guarded = false;
					break;
				}
			}

			if (guarded) {
				prev = link;
			} else {
				if (0 == nitems++)
					head = tail = link;
				else
					tail->next = link;
				tail = link;

				prev->next = link->next;
			}
		}

		// Move the items to the free list.
		if (nitems > 0) {
			cdata->cache_size -= nitems;

			// Make sure the guard load ops above are not reordered
			// wrt the CAS below.
			mm_memory_fence();

			struct mm_link *old_head = mm_link_shared_head(&pool->free_list);
			for (uint32_t b = 0; ; b = mm_backoff(b)) {
				tail->next = old_head;
				struct mm_link *cur_head
					= mm_link_cas_head(&pool->free_list, old_head, head);
				if (cur_head == old_head)
					break;
				old_head = cur_head;
			}
		}
	}

	LEAVE();
}

void *
mm_pool_shared_alloc(struct mm_pool *pool)
{
	return mm_pool_shared_alloc_low(mm_core_selfid(), pool);
}

void
mm_pool_shared_free(struct mm_pool *pool, void *item)
{
	mm_pool_shared_free_low(mm_core_selfid(), pool, item);
}

#endif

void
mm_pool_prepare_shared(struct mm_pool *pool, const char *name, uint32_t item_size)
{
	ENTER();

	mm_pool_prepare_low(pool, name, &mm_shared_space.xarena, item_size);

	pool->shared = true;
	pool->global = false;

#if ENABLE_SMP
	pool->shared_data.grow_lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;

	char *cdata_name = mm_format(&mm_global_arena, "'%s' memory pool", name);
	MM_CDATA_ALLOC(mm_domain_self(), cdata_name, pool->shared_data.cdata);
	mm_core_t n = mm_core_getnum();
	for (mm_core_t i = 0; i < n; i++) {
		struct mm_pool_shared_cdata *cdata =
			MM_CDATA_DEREF(i, pool->shared_data.cdata);

		mm_link_init(&cdata->cache);
		cdata->item_guard = 0;
		cdata->guard_buffer = mm_global_calloc(n, sizeof(struct mm_link *));
		cdata->cache_size = 0;
		cdata->cache_full = false;
	}
	mm_global_free(cdata_name);

	pool->alloc_item = mm_pool_shared_alloc;
	pool->free_item = mm_pool_shared_free;
#else
	pool->alloc_item = mm_pool_local_alloc;
	pool->free_item = mm_pool_local_free;
#endif

	LEAVE();
}

/**********************************************************************
 * Global pools.
 **********************************************************************/

static void *
mm_pool_global_alloc(struct mm_pool *pool)
{
	ENTER();

	mm_thread_lock(&pool->global_data.free_lock);

	void *item;
	if (!mm_link_empty(&pool->free_list)) {
		item = mm_link_delete_head(&pool->free_list);

		mm_thread_unlock(&pool->global_data.free_lock);
	} else {
		mm_thread_unlock(&pool->global_data.free_lock);

		mm_thread_lock(&pool->global_data.grow_lock);
		item = mm_pool_alloc_new(pool);
		mm_thread_unlock(&pool->global_data.grow_lock);
	}

	LEAVE();
	return item;
}

static void
mm_pool_global_free(struct mm_pool *pool, void *item)
{
	ENTER();
	ASSERT(mm_pool_contains(pool, item));

	mm_thread_lock(&pool->global_data.free_lock);
	mm_link_insert(&pool->free_list, (struct mm_link *) item);
	mm_thread_unlock(&pool->global_data.free_lock);

	LEAVE();
}

void
mm_pool_prepare_global(struct mm_pool *pool, const char *name, uint32_t item_size)
{
	ENTER();

	mm_pool_prepare_low(pool, name, &mm_global_arena, item_size);

	pool->shared = false;
	pool->global = true;

	pool->global_data.free_lock = (mm_thread_lock_t) MM_THREAD_LOCK_INIT;
	pool->global_data.grow_lock = (mm_thread_lock_t) MM_THREAD_LOCK_INIT;

	pool->alloc_item = mm_pool_global_alloc;
	pool->free_item = mm_pool_global_free;

	LEAVE();
}
