/*
 * pool.c - MainMemory memory pools.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "pool.h"

#include "util.h"

#define MM_POOL_FREE_NIL	0xffffffff
#define MM_POOL_FREE_PAD	0xdeadbeaf

struct mm_free_item
{
	uint32_t pad;
	uint32_t next;
};

static inline size_t
mm_pool_grow_size(uint32_t item_size, uint32_t pool_size)
{
	ASSERT(item_size < 0x200);

	// Round the size up to a 4k multiple.
	size_t size = (pool_size == 0 ? 0x1000 : (item_size * pool_size + 0xfff) & ~0xfff);

	// Double the size and subtract the malloc overhead.
	size *= 2;
	size -= 16;

	// Return next pool size.
	return size / item_size;
}

void
mm_pool_init(struct mm_pool *pool, size_t item_size)
{
	ENTER();

	if (item_size < sizeof(struct mm_free_item))
		item_size = sizeof(struct mm_free_item);

	pool->item_size = item_size;
	pool->pool_size = mm_pool_grow_size(item_size, 0);

	pool->item_count = 0;
	pool->free_index = MM_POOL_FREE_NIL;

	pool->pool_data = mm_alloc(pool->pool_size * pool->item_size);

	LEAVE();
}

void
mm_pool_discard(struct mm_pool *pool)
{
	ENTER();

	mm_free(pool->pool_data);

	LEAVE();
}

void *
mm_pool_idx2ptr(struct mm_pool *pool, uint32_t index)
{
	ASSERT(index < pool->item_count);

	return (char *) pool->pool_data + (size_t) index * pool->item_size;
}

uint32_t
mm_pool_ptr2idx(struct mm_pool *pool, void *item)
{
	ASSERT(item >= pool->pool_data);
	ASSERT(item < pool->pool_data + pool->item_count * pool->item_size);

	size_t offset = (char *) item - (char *) pool->pool_data;
	ASSERT((offset % pool->item_size) == 0);

	return (uint32_t) (offset / pool->item_size);
}


void *
mm_pool_alloc(struct mm_pool *pool)
{
	ENTER();

	void *item;
	if (pool->free_index != MM_POOL_FREE_NIL) {
		item = (char *) pool->pool_data + (size_t) pool->free_index * pool->item_size;
		pool->free_index = ((struct mm_free_item *) item)->next;
	} else {
		if (unlikely(pool->item_count == pool->pool_size)) {
			/* Check for integer overflow. */
			uint32_t size = mm_pool_grow_size(pool->item_size,
							  pool->pool_size);
			if (unlikely(size < pool->pool_size)) {
				mm_error(0, "memory pool overflow");
				return NULL;
			}
			mm_print("new memory pool size: %u (%lu bytes))",
				 size, (unsigned long) pool->item_size * size);

			pool->pool_size = size;
			pool->pool_data = mm_realloc(pool->pool_data, size * pool->item_size);
		}
		item = pool->pool_data + pool->item_size * pool->item_count++;
	}

	LEAVE();
	return item;
}

void
mm_pool_free(struct mm_pool *pool, void *item)
{
	ENTER();

	((struct mm_free_item *) item)->pad = MM_POOL_FREE_PAD;
	((struct mm_free_item *) item)->next = pool->free_index;
	pool->free_index = mm_pool_ptr2idx(pool, item);

	LEAVE();
}
