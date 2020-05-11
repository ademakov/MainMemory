/*
 * base/memory/span.h - MainMemory virtual memory spans.
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

#ifndef BASE_MEMORY_SPAN_H
#define BASE_MEMORY_SPAN_H

#include "common.h"
#include "base/list.h"
#include "base/report.h"

/*
 * A memory span is a big memory chunk allocated with a mmap() system call.
 * A span always starts at an address that is aligned to a 2 MiB boundary.
 * At this address there is always a struct that describes the span itself.
 *
 * There are two kinds of spans:
 *   -- heap spans are used to store a number of smaller memory chunks;
 *   -- huge spans are used to store a single chunk that doesn't fit a heap
 *      span.
 *
 * Heap spans always take 2 MiB of memory. Huge spans vary in size.
 */

/* Span alignment values. */
#define MM_MEMORY_SPAN_ALIGNMENT	(((size_t) 1) << 21)
#define MM_MEMORY_SPAN_ALIGNMENT_MASK	(MM_MEMORY_SPAN_ALIGNMENT - 1)

/* The size of a span that keeps smaller objects inside. Such spans comprise
   a memory heap. Thus it is called a 'heap' span. */
#define MM_MEMORY_SPAN_HEAP_SIZE	MM_MEMORY_SPAN_ALIGNMENT

/* The token value that tags heap spans. */
#define MM_MEMORY_SPAN_HEAP_TAG		((size_t) 0)

/* Span descriptor. */
struct mm_memory_span
{
	/* The heap tag or the usable size for a huge span. */
	size_t tag_or_size;
	/* The memory size that is actually mmap()-ed. */
	size_t virtual_size;

	/* The execution context the span belongs to (if any). */
	struct mm_context *context;

	/* The memory cache the span belongs to. */
	struct mm_memory_cache *cache;
};

/* Huge span header. */
union mm_memory_span_huge
{
	struct mm_memory_span span;
	uint8_t padding[MM_CACHELINE];
};

/* Get span descriptor for an address within 2MiB from its start. */
static inline struct mm_memory_span * NONNULL(1)
mm_memory_span_from_ptr(const void *ptr)
{
	return (struct mm_memory_span *) ((uintptr_t) ptr & ~MM_MEMORY_SPAN_ALIGNMENT_MASK);
}

/* Get the actual size of virtual memory occupied by the span. */
static inline size_t NONNULL(1)
mm_memory_span_virtual_size(const struct mm_memory_span *span)
{
	return span->virtual_size;
}

/* Check to see if the span is for regular heap allocation. */
static inline bool NONNULL(1)
mm_memory_span_heap(const struct mm_memory_span *span)
{
	return (span->tag_or_size == MM_MEMORY_SPAN_HEAP_TAG);
}

/* Check to see if the span is for a single huge chunk. */
static inline bool NONNULL(1)
mm_memory_span_huge(const struct mm_memory_span *span)
{
	return (span->tag_or_size != MM_MEMORY_SPAN_HEAP_TAG);
}

static inline size_t NONNULL(1)
mm_memory_span_huge_size(const struct mm_memory_span *span)
{
	ASSERT(mm_memory_span_huge(span));
	return span->tag_or_size;
}

static inline void * NONNULL(1)
mm_memory_span_huge_data(const struct mm_memory_span *span)
{
	ASSERT(mm_memory_span_huge(span));
	return (uint8_t *) span + sizeof(union mm_memory_span_huge);
}

struct mm_memory_span * NONNULL(1)
mm_memory_span_create_heap(struct mm_memory_cache *cache);

struct mm_memory_span * NONNULL(1)
mm_memory_span_create_huge(struct mm_memory_cache *cache, size_t size);

void NONNULL(1)
mm_memory_span_destroy(struct mm_memory_span *span);

#endif /* BASE_MEMORY_SPAN_H */
