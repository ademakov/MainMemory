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

#include "util.h"

#define MM_POOL_BLOCK_SIZE	0x2000

struct mm_pool_free_item
{
	struct mm_pool_free_item *next;
};

void
mm_pool_init(struct mm_pool *pool, const char *pool_name, uint32_t item_size)
{
	ENTER();
	ASSERT(item_size < 0x200);

	if (item_size < sizeof(struct mm_pool_free_item))
		item_size = sizeof(struct mm_pool_free_item);

	mm_print("make the '%s' memory pool with element size %u",
		 pool_name, item_size);

	pool->item_last = 0;
	pool->item_size = item_size;

	pool->block_capacity = MM_POOL_BLOCK_SIZE / item_size;
	pool->block_array_used = 1;
	pool->block_array_size = 4;

	// Allocate the block container.
	pool->block_array = mm_alloc(pool->block_array_size * sizeof(char *));

	// Allocate the first block.
	pool->block_array[0] = mm_alloc(MM_POOL_BLOCK_SIZE);
	pool->block_cur_ptr = pool->block_array[0];
	pool->block_end_ptr = pool->block_cur_ptr +  pool->block_capacity * pool->item_size;

	pool->free_list = NULL;
	pool->pool_name = mm_strdup(pool_name);

	LEAVE();
}

void
mm_pool_discard(struct mm_pool *pool)
{
	ENTER();

	for (uint32_t i = 0; i < pool->block_array_used; i++)
		mm_free(pool->block_array[i]);
	mm_free(pool->block_array);
	mm_free(pool->pool_name);

	LEAVE();
}

void *
mm_pool_idx2ptr(struct mm_pool *pool, uint32_t n)
{
	if (unlikely(n >= pool->item_last))
		return NULL;

	uint32_t block = n / pool->block_capacity;
	uint32_t index = n % pool->block_capacity;
	return pool->block_array[block] + index * pool->item_size;
}

uint32_t
mm_pool_ptr2idx(struct mm_pool *pool, void *item)
{
	uint32_t block = 0;
	while (block < pool->block_array_used
	       && ((char *) item) < pool->block_array[block]
	       && (char *) item >= pool->block_array[block] + MM_POOL_BLOCK_SIZE)
		++block;

	if (unlikely(block == pool->block_array_used))
		return MM_POOL_INDEX_INVALID;

	uint32_t index = ((char *) item - pool->block_array[block]) / pool->item_size;

	return block * pool->block_capacity + index;
}

void *
mm_pool_alloc(struct mm_pool *pool)
{
	ENTER();

	void *item;
	if (pool->free_list != NULL) {
		item = pool->free_list;
		pool->free_list = pool->free_list->next;
	} else {
		if (unlikely(pool->block_cur_ptr == pool->block_end_ptr)) {
			mm_print("grow the '%s' memory pool with element size %u",
				 pool->pool_name, pool->item_size);

			// Check for 32-bit integer overflow.
			uint32_t total_capacity = pool->block_capacity * pool->block_array_used;
			if (unlikely(total_capacity > (total_capacity + pool->block_capacity)))
				mm_fatal(0, "the '%s' memory pool overflow", pool->pool_name);

			if (pool->block_array_used == pool->block_array_size) {
				pool->block_array_size *= 2;
				pool->block_array = mm_realloc(
					pool->block_array,
					pool->block_array_size * sizeof(char *));
			}

			pool->block_array[pool->block_array_used] = mm_alloc(MM_POOL_BLOCK_SIZE);
			pool->block_cur_ptr = pool->block_array[pool->block_array_used];
			pool->block_end_ptr = pool->block_cur_ptr + pool->block_capacity * pool->item_size;

			pool->block_array_used++;
		}

		item = pool->block_cur_ptr;
		pool->block_cur_ptr += pool->item_size;

		pool->item_last++;
	}

	LEAVE();
	return item;
}

void
mm_pool_free(struct mm_pool *pool, void *item)
{
	ENTER();

	struct mm_pool_free_item *free_item = item;
	free_item->next = pool->free_list;
	pool->free_list = free_item;

	LEAVE();
}
