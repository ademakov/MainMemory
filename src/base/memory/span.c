/*
 * base/memory/span.c - MainMemory virtual memory span.
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

#include "base/memory/span.h"

#include "base/bitops.h"
#include "base/report.h"
#include "base/memory/cache.h"

#include <sys/mman.h>

static void
mm_memory_free_space(void *const addr, const size_t size)
{
	if (unlikely(munmap(addr, size) < 0))
		mm_fatal(errno, "munmap()");
}

static void *
mm_memory_alloc_space(const size_t size, const size_t addr_mask)
{
	ASSERT(mm_is_pow2(size));

	// Allocate a span speculatively assuming that it will be aligned as required.
	void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	// If failed to align then repeat allocation with required padding.
	if ((((uintptr_t) addr) & addr_mask) != 0) {
		mm_memory_free_space(addr, size);

		const size_t upsized_size = size + addr_mask - MM_PAGE_SIZE + 1;
		if (upsized_size < size) {
			// integer aritmetic overflow
			errno = EOVERFLOW;
			return NULL;
		}

		void *upsized_addr = mmap(NULL, upsized_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (upsized_addr == MAP_FAILED)
			return NULL;

		addr = (void *) ((((uintptr_t) upsized_addr) + addr_mask) & addr_mask);
		const size_t leading_size = addr - upsized_addr;
		const size_t trailing_size = upsized_size - leading_size - size;
		if (leading_size)
			mm_memory_free_space(upsized_addr, leading_size);
		if (trailing_size)
			mm_memory_free_space(addr + size, trailing_size);
	}

	return addr;
}

struct mm_memory_span * NONNULL(1)
mm_memory_span_create_heap(struct mm_memory_cache *const cache, const bool active)
{
	ENTER();

	struct mm_memory_span *span = mm_memory_alloc_space(MM_MEMORY_SPAN_HEAP_SIZE, MM_MEMORY_SPAN_ALIGNMENT_MASK);
	if (likely(span != NULL)) {
		span->type_and_size = active ? MM_MEMORY_SPAN_ACTIVE_HEAP : MM_MEMORY_SPAN_STAGING_HEAP;
		span->resident_size = MM_MEMORY_SPAN_HEAP_SIZE;

		span->cache = cache;
		span->context = cache->context;

		if (active) {
			VERIFY(cache->active == NULL);
			cache->active = span;
		} else {
			mm_list_insert(&cache->heap, &span->cache_link);
		}
	}

	LEAVE();
	return span;
}

struct mm_memory_span * NONNULL(1)
mm_memory_span_create_huge(struct mm_memory_cache *const cache, const size_t size)
{
	ENTER();

	struct mm_memory_span *span = NULL;

	const size_t span_size = mm_round_up(sizeof(struct mm_memory_span) + size, MM_PAGE_SIZE);
	if (unlikely(span_size < size)) {
		// integer aritmetic overflow
		errno = EOVERFLOW;
		goto leave;
	}

	span = mm_memory_alloc_space(span_size, MM_MEMORY_SPAN_ALIGNMENT_MASK);
	if (likely(span != NULL)) {
		span->type_and_size = span_size;
		span->resident_size = span_size;

		span->cache = cache;
		span->context = cache->context;

		mm_list_insert(&cache->huge, &span->cache_link);
	}

leave:
	LEAVE();
	return span;
}

void NONNULL(1)
mm_memory_span_activate(struct mm_memory_span *const span)
{
	ENTER();
	ASSERT(mm_memory_span_staging(span));
	ASSERT(span->cache->active == NULL);

	span->type_and_size = MM_MEMORY_SPAN_ACTIVE_HEAP;
	mm_list_delete(&span->cache_link);
	span->cache->active = span;

	LEAVE();
}

void NONNULL(1)
mm_memory_span_deactivate(struct mm_memory_span *const span)
{
	ENTER();
	ASSERT(mm_memory_span_active(span));
	ASSERT(span->cache->active == span);

	span->type_and_size = MM_MEMORY_SPAN_STAGING_HEAP;
	mm_list_insert(&span->cache->heap, &span->cache_link);
	span->cache->active = NULL;

	LEAVE();
}

void NONNULL(1)
mm_memory_span_destroy(struct mm_memory_span *const span)
{
	ENTER();

	mm_memory_free_space(span, mm_memory_span_virtual_size(span));

	LEAVE();
}
