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

#include "base/report.h"

#include <sys/mman.h>

static void
mm_memory_free_space(void *addr, size_t size)
{
	if (unlikely(munmap(addr, size) < 0))
		mm_fatal(errno, "munmap()");
}

static void *
mm_memory_alloc_space(const size_t size, const size_t addr_mask)
{
	ASSERT(size != 0);
	ASSERT((size & (MM_PAGE_SIZE - 1)) == 0);

	// Allocate a span speculatively assuming that it will be aligned as required.
	void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	// If failed to align then repeat allocation with required padding.
	if ((((uintptr_t) addr) & addr_mask) != 0) {
		mm_memory_free_space(addr, size);

		const size_t upsized_size = size + addr_mask - MM_PAGE_SIZE + 1;
		if (upsized_size < size) // check for integer aritmetic overflow
			return NULL;

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

struct mm_memory_span *
mm_memory_span_create_heap(struct mm_context *context, struct mm_memory_cache *cache)
{
	ENTER();

	struct mm_memory_span *span = mm_memory_alloc_space(MM_MEMORY_SPAN_HEAP_SIZE,
							    MM_MEMORY_SPAN_ALIGNMENT_MASK);
	span->type_and_size = 1;
	span->resident_size = MM_MEMORY_SPAN_HEAP_SIZE;
	span->context = context;
	span->cache = cache;
	// TODO: cache_link

	LEAVE();
	return span;
}

struct mm_memory_span *
mm_memory_span_create_huge(struct mm_context *context, struct mm_memory_cache *cache, size_t size)
{
	ENTER();

	size_t span_size = sizeof(struct mm_memory_span) + size;
	span_size = (size + MM_PAGE_SIZE) & ~MM_PAGE_SIZE;
	if (unlikely(span_size < size)) // check for integer aritmetic overflow
		return NULL;

	struct mm_memory_span *span = mm_memory_alloc_space(span_size,
							    MM_MEMORY_SPAN_ALIGNMENT_MASK);
	span->type_and_size = size;
	span->resident_size = size;
	span->context = context;
	span->cache = cache;
	// TODO: cache_link

	LEAVE();
	return span;
}

void
mm_memory_span_destroy(struct mm_memory_span *span)
{
	ENTER();

	mm_memory_free_space(span, mm_memory_span_size(span));

	LEAVE();
}
