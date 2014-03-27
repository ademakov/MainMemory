/*
 * pool.c - MainMemory memory pools.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#include "pool.h"

#include "alloc.h"
#include "log.h"
#include "trace.h"
#include "util.h"

#define MM_POOL_BLOCK_SIZE	(0x2000)

struct mm_pool_free_item
{
	struct mm_pool_free_item *next;
};

void
mm_pool_prepare(struct mm_pool *pool,
		const char *pool_name,
		const struct mm_allocator *alloc,
		uint32_t item_size)
{
	ENTER();
	ASSERT(item_size < 0x200);

	if (item_size < sizeof(struct mm_pool_free_item))
		item_size = sizeof(struct mm_pool_free_item);

	mm_brief("make the '%s' memory pool with element size %u",
		 pool_name, item_size);

	pool->item_last = 0;
	pool->item_size = item_size;

	pool->block_capacity = MM_POOL_BLOCK_SIZE / item_size;
	pool->block_array_used = 0;
	pool->block_array_size = 0;

	pool->global = (alloc == &mm_alloc_global);
	pool->free_lock = MM_THREAD_LOCK_INIT;
	pool->grow_lock = MM_THREAD_LOCK_INIT;

	pool->alloc = alloc;
	pool->block_array = NULL;
	pool->block_cur_ptr = NULL;
	pool->block_end_ptr = NULL;

	pool->free_list = NULL;
	pool->pool_name = mm_strdup(pool_name);

	LEAVE();
}

void
mm_pool_cleanup(struct mm_pool *pool)
{
	ENTER();

	for (uint32_t i = 0; i < pool->block_array_used; i++)
		pool->alloc->free(pool->block_array[i]);
	pool->alloc->free(pool->block_array);

	mm_free(pool->pool_name);

	LEAVE();
}

void *
mm_pool_idx2ptr(struct mm_pool *pool, uint32_t item_idx)
{
	uint32_t block = item_idx / pool->block_capacity;
	uint32_t index = item_idx % pool->block_capacity;

	if (pool->global)
		mm_thread_lock(&pool->grow_lock);

	void *item_ptr;
	if (unlikely(item_idx >= pool->item_last))
		item_ptr = NULL;
	else
		item_ptr = pool->block_array[block] + index * pool->item_size;

	if (pool->global)
		mm_thread_unlock(&pool->grow_lock);

	return item_ptr;
}

uint32_t
mm_pool_ptr2idx(struct mm_pool *pool, void *item_ptr)
{
	char *start = NULL;
	uint32_t block = 0;

	if (pool->global)
		mm_thread_lock(&pool->grow_lock);

	while (block < pool->block_array_used) {
		char *s_ptr = pool->block_array[block];
		char *e_ptr = s_ptr + MM_POOL_BLOCK_SIZE;
		if((char *) item_ptr >= s_ptr && (char *) item_ptr < e_ptr) {
			start = s_ptr;
			break;
		}
		++block;
	}

	if (pool->global)
		mm_thread_unlock(&pool->grow_lock);

	if (unlikely(start == NULL))
		return MM_POOL_INDEX_INVALID;

	uint32_t index = ((char *) item_ptr - start) / pool->item_size;

	return block * pool->block_capacity + index;
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

		pool->block_array = pool->alloc->realloc(
			pool->block_array,
			pool->block_array_size * sizeof(char *));
	}

	// Allocate a new memory block.
	char *block = pool->alloc->alloc(MM_POOL_BLOCK_SIZE);
	pool->block_array[pool->block_array_used] = block;
	pool->block_array_used++;

	pool->block_cur_ptr = block;
	pool->block_end_ptr = block + pool->block_capacity * pool->item_size;

	mm_brief("grow the '%s' memory pool to %u elements, occupy %lu bytes",
		 pool->pool_name,
		 pool->block_capacity * pool->block_array_used,
		 (unsigned long) MM_POOL_BLOCK_SIZE * pool->block_array_used);

	LEAVE();
}

void *
mm_pool_alloc(struct mm_pool *pool)
{
	ENTER();

	if (pool->global)
		mm_thread_lock(&pool->free_lock);

	void *item;
	if (pool->free_list != NULL) {
		item = pool->free_list;
		pool->free_list = pool->free_list->next;

		if (pool->global)
			mm_thread_unlock(&pool->free_lock);

	} else {
		if (pool->global) {
			mm_thread_lock(&pool->grow_lock);
			mm_thread_unlock(&pool->free_lock);
		}

		if (unlikely(pool->block_cur_ptr == pool->block_end_ptr))
			mm_pool_grow(pool);

		item = pool->block_cur_ptr;
		pool->block_cur_ptr += pool->item_size;
		pool->item_last++;

		if (pool->global)
			mm_thread_unlock(&pool->grow_lock);
	}

	LEAVE();
	return item;
}

void
mm_pool_free(struct mm_pool *pool, void *item)
{
	ENTER();

	if (pool->global)
		mm_thread_lock(&pool->free_lock);

	struct mm_pool_free_item *free_item = item;
	free_item->next = pool->free_list;
	pool->free_list = free_item;

	if (pool->global)
		mm_thread_unlock(&pool->free_lock);

	LEAVE();
}
