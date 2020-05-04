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

/*
 * A memory allocation cache.
 */
struct mm_memory_cache
{
	/* The active span to allocate memory from. */
	struct mm_memory_heap *active;

	/* The inactive spans to gather freed memory. */
	struct mm_list staging;

	/* All the spans that belong to the cache. */
	struct mm_list spans;

	/* The execution context the cache belongs to. */
	struct mm_context *context;
};

void NONNULL(1)
mm_memory_cache_prepare(struct mm_memory_cache *cache, struct mm_context *context);

void NONNULL(1)
mm_memory_cache_cleanup(struct mm_memory_cache *cache);

void * NONNULL(1) MALLOC
mm_memory_cache_alloc(struct mm_memory_cache *cache, size_t size);

void * NONNULL(1) MALLOC
mm_memory_cache_zalloc(struct mm_memory_cache *cache, size_t size);

void * NONNULL(1) MALLOC
mm_memory_cache_aligned_alloc(struct mm_memory_cache *cache, size_t align, size_t size);

void * NONNULL(1) MALLOC
mm_memory_cache_calloc(struct mm_memory_cache *cache, size_t count, size_t size);

void * NONNULL(1) MALLOC
mm_memory_cache_realloc(struct mm_memory_cache *cache, void *ptr, size_t size);

void NONNULL(1)
mm_memory_cache_free(struct mm_memory_cache *cache, void *ptr);

size_t
mm_memory_cache_chunk_size(const void *ptr);

#endif /* BASE_MEMORY_CACHE_H */
