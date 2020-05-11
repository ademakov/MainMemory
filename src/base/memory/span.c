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
#include "base/exit.h"
#include "base/memory/cache.h"

#include <sys/mman.h>

static void
mm_memory_free_space(void *const addr, const size_t size)
{
	if (unlikely(munmap(addr, size) < 0))
		mm_panic("panic: failed munmap() call\n");
}

static void *
mm_memory_alloc_space(const size_t size, const size_t addr_mask)
{
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

		addr = (void *) ((((uintptr_t) upsized_addr) + addr_mask) & ~addr_mask);
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
mm_memory_span_create_heap(struct mm_memory_cache *const cache)
{
	struct mm_memory_span *span = mm_memory_alloc_space(MM_MEMORY_SPAN_HEAP_SIZE, MM_MEMORY_SPAN_ALIGNMENT_MASK);
	if (likely(span != NULL)) {
		span->tag_or_size = MM_MEMORY_SPAN_HEAP_TAG;
		span->virtual_size = MM_MEMORY_SPAN_HEAP_SIZE;

		span->cache = cache;
		span->context = cache->context;
	}
	return span;
}

struct mm_memory_span * NONNULL(1)
mm_memory_span_create_huge(struct mm_memory_cache *const cache, const size_t size)
{
	const size_t total_size = mm_round_up(sizeof(union mm_memory_span_huge) + size, MM_PAGE_SIZE);
	if (unlikely(total_size < size)) {
		// integer aritmetic overflow
		errno = EOVERFLOW;
		return NULL;
	}

	struct mm_memory_span *span = mm_memory_alloc_space(total_size, MM_MEMORY_SPAN_ALIGNMENT_MASK);
	if (likely(span != NULL)) {
		span->tag_or_size = total_size - sizeof(union mm_memory_span_huge);
		span->virtual_size = total_size;

		span->cache = cache;
		span->context = cache->context;
	}
	return span;
}

void NONNULL(1)
mm_memory_span_destroy(struct mm_memory_span *const span)
{
	mm_memory_free_space(span, mm_memory_span_virtual_size(span));
}
