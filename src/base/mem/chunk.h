/*
 * base/mem/chunk.h - MainMemory memory chunks.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#ifndef BASE_MEM_CHUNK_H
#define BASE_MEM_CHUNK_H

#include "common.h"
#include "base/list.h"
#include "base/mem/alloc.h"

struct mm_thread;

/**********************************************************************
 * Chunk Tags.
 **********************************************************************/

#define MM_CHUNK_SHARED_MAX	((int) 3)

#define MM_CHUNK_IDX_TO_TAG(i)	((mm_chunk_t) ~(i))
#define MM_CHUNK_TAG_TO_IDX(t)	(~((int)(int16_t) (t)))

#define MM_CHUNK_GLOBAL		MM_CHUNK_IDX_TO_TAG(0)
#define MM_CHUNK_COMMON		MM_CHUNK_IDX_TO_TAG(1)
#define MM_CHUNK_REGULAR	MM_CHUNK_IDX_TO_TAG(2)

#define MM_CHUNK_IS_SHARED(t)	((t) > MM_CHUNK_IDX_TO_TAG(MM_CHUNK_SHARED_MAX))

typedef uint16_t mm_chunk_t;

/**********************************************************************
 * Chunk Access.
 **********************************************************************/

#define MM_CHUNK_OVERHEAD (sizeof(struct mm_chunk) + MM_ALLOC_OVERHEAD)

struct mm_chunk_base
{
	union
	{
		struct mm_slink slink;
		struct mm_qlink qlink;
	};
	mm_chunk_t tag;
};

/* A chunk of memory that could be chained together with other chunks and
   passed from one thread to another. Useful for I/O buffers and such. */
struct mm_chunk
{
	struct mm_chunk_base base;
	char data[];
};

static inline mm_chunk_t
mm_chunk_base_gettag(const struct mm_chunk_base *chunk)
{
	return chunk->tag;
}

static inline mm_chunk_t
mm_chunk_gettag(const struct mm_chunk *chunk)
{
	return mm_chunk_base_gettag(&chunk->base);
}

static inline size_t
mm_chunk_base_getsize(const struct mm_chunk_base *chunk)
{
	size_t size;
#if ENABLE_PARANOID
	if (mm_chunk_base_gettag(chunk) == MM_CHUNK_GLOBAL)
		size = mm_global_getallocsize(chunk);
	else
		size = mm_mspace_getallocsize(chunk);
#else
	size = mm_mspace_getallocsize(chunk);
#endif
	return size - sizeof(struct mm_chunk);
}

static inline size_t
mm_chunk_getsize(const struct mm_chunk *chunk)
{
	return mm_chunk_base_getsize(&chunk->base);
}

/**********************************************************************
 * Chunk Creation and Destruction.
 **********************************************************************/

struct mm_chunk *
mm_chunk_create_global(size_t size);

struct mm_chunk *
mm_chunk_create_common(size_t size);

struct mm_chunk *
mm_chunk_create_regular(size_t size);

struct mm_chunk *
mm_chunk_create_private(size_t size);

struct mm_chunk *
mm_chunk_create(size_t size);

void __attribute__((nonnull(1)))
mm_chunk_destroy(struct mm_chunk *chunk);

void
mm_chunk_destroy_chain(struct mm_slink *link);

void __attribute__((nonnull(1)))
mm_chunk_enqueue_deferred(struct mm_thread *thread, bool flush);

#endif /* BASE_MEM_CHUNK_H */
