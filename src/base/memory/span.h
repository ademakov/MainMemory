/*
 * base/memory/span.h - MainMemory virtual memory span.
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

/*
 * A memory span is a large memory chunk allocated with a mmap() system call.
 * A span always starts at an address that is aligned to a 2 MiB boundary. At
 * this address there is always a struct that describes the span itself.
 */

/* Span alignment values. */
#define MM_MEMORY_SPAN_ALIGNMENT	(((size_t) 1) << 21)
#define MM_MEMORY_SPAN_ALIGNMENT_MASK	(MM_MEMORY_SPAN_ALIGNMENT - 1)

/* Size of a regular span used as a storage for smaller objects. */
#define MM_MEMORY_SPAN_HEAP_SIZE	MM_MEMORY_SPAN_ALIGNMENT

/* Span descriptor. */
struct mm_memory_span
{
	/* The type and size of the span. */
	size_t type_and_size;
	/* The size that is actually used for data storage. */
	size_t resident_size;

	/* The execution context the span belongs to. */
	struct mm_context *context;
	/* The memory cache the span belongs to. */
	struct mm_memory_cache *cache;
	struct mm_link cache_link;
};

/* Get span descriptor for an address within 2MiB from its start. */
static inline struct mm_memory_span *
mm_memory_span_from_ptr(void *ptr)
{
	return (struct mm_memory_span *) ((uintptr_t) ptr & ~MM_MEMORY_SPAN_ALIGNMENT_MASK);
}

static inline bool
mm_memory_span_heap(struct mm_memory_span *span)
{
	return (span->type_and_size == 1);
}

static inline bool
mm_memory_span_huge(struct mm_memory_span *span)
{
	return (span->type_and_size != 1);
}

static inline size_t
mm_memory_span_size(struct mm_memory_span *span)
{
	return mm_memory_span_heap(span) ? MM_MEMORY_SPAN_HEAP_SIZE : span->type_and_size;
}

struct mm_memory_span *
mm_memory_span_create_heap(struct mm_context *context, struct mm_memory_cache *cache);

struct mm_memory_span *
mm_memory_span_create_huge(struct mm_context *context, struct mm_memory_cache *cache, size_t size);

void
mm_memory_span_destroy(struct mm_memory_span *span);

#endif /* BASE_MEMORY_SPAN_H */
