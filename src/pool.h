/*
 * pool.h - MainMemory memory pools.
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

#ifndef POOL_H
#define POOL_H

#include "common.h"
#include "cdata.h"
#include "list.h"
#include "lock.h"

/* Forward declaration. */
struct mm_allocator;

#define MM_POOL_INDEX_INVALID	((uint32_t) -1)

#if ENABLE_SMP

struct mm_pool_shared_cdata
{
	/* The cache of free work items. */
	struct mm_link cache;
	/* The number of items in the free cache. */
	uint32_t cache_size;
	/* The cache is full. */
	bool cache_full;
};

struct mm_pool_shared
{
	mm_task_lock_t free_lock;
	mm_task_lock_t grow_lock;

	MM_CDATA(struct mm_pool_shared_cdata, cdata);
};

#endif

struct mm_pool_global
{
	mm_thread_lock_t free_lock;
	mm_thread_lock_t grow_lock;
};

struct mm_pool
{
	struct mm_link free_list;
	char *block_cur_ptr;
	char *block_end_ptr;
	char **block_array;

	uint32_t item_size;
	uint32_t item_last;
	uint32_t block_capacity;
	uint32_t block_array_used;
	uint32_t block_array_size;

	bool shared;
	bool global;

	union {
#if ENABLE_SMP
		struct mm_pool_shared shared_data;
#endif
		struct mm_pool_global global_data;
	};

	const struct mm_allocator *alloc;

	void * (*alloc_item)(struct mm_pool *pool);
	void (*free_item)(struct mm_pool *pool, void *item);

	char *pool_name;
};

void mm_pool_prepare(struct mm_pool *pool, const char *name, uint32_t item_size)
	__attribute__((nonnull(1, 2)));

void mm_pool_prepare_shared(struct mm_pool *pool, const char *name, uint32_t item_size)
	__attribute__((nonnull(1, 2)));

void mm_pool_prepare_global(struct mm_pool *pool, const char *name, uint32_t item_size)
	__attribute__((nonnull(1, 2)));

void mm_pool_cleanup(struct mm_pool *pool)
	__attribute__((nonnull(1)));

void * mm_pool_idx2ptr(struct mm_pool *pool, uint32_t index)
	__attribute__((nonnull(1)));

uint32_t mm_pool_ptr2idx(struct mm_pool *pool, const void *item)
	__attribute__((nonnull(1, 2)));

bool mm_pool_contains(struct mm_pool *pool, const void *item)
	__attribute__((nonnull(1)));

static inline void *
mm_pool_alloc(struct mm_pool *pool)
{
	return (pool->alloc_item)(pool);
}

static inline void
mm_pool_free(struct mm_pool *pool, void *item)
{
	(pool->free_item)(pool, item);
}

#endif /* POOL_H */
