/*
 * base/memory/cache.h - MainMemory virtual memory allocation cache.
 *
 * Copyright (C) 2019-2020  Aleksey Demakov
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

#ifndef BASE_MEMORY_CACHE_H
#define BASE_MEMORY_CACHE_H

#include "common.h"
#include "base/list.h"

/* Forward declarations. */
struct mm_memory_heap;

/*
 * Per-thread memory cache.
 */
struct mm_memory_cache
{
	/* The active regular span to allocate memory from. */
	struct mm_memory_heap *active;
	/* The execution context the cache belongs to. */
	struct mm_context *context;
	/* Inactive spans to gather released memory. */
	struct mm_list heap;
	/* Active spans with huge chunks inside. */
	struct mm_list huge;
};

void NONNULL(1)
mm_memory_cache_prepare(struct mm_memory_cache *cache, struct mm_context *context);

void NONNULL(1)
mm_memory_cache_cleanup(struct mm_memory_cache *cache);

void * NONNULL(1)
mm_memory_cache_alloc(struct mm_memory_cache *cache, size_t size);

void NONNULL(1, 2)
mm_memory_cache_free(struct mm_memory_cache *cache, void *ptr);

#endif /* BASE_MEMORY_CACHE_H */
