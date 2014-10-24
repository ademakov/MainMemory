/*
 * base/mem/chunk.h - MainMemory memory chunks.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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
#include "base/mem/arena.h"

/**********************************************************************
 * Chunk Tags.
 **********************************************************************/

#define MM_CHUNK_ARENA_MAX	((int) 32)

#define MM_CHUNK_IDX_TO_TAG(i)	((mm_chunk_tag_t) ~(i))
#define MM_CHUNK_TAG_TO_IDX(t)	(~((int)(int16_t) (t)))

#define MM_CHUNK_GLOBAL		MM_CHUNK_IDX_TO_TAG(0)
#define MM_CHUNK_COMMON		MM_CHUNK_IDX_TO_TAG(1)

#define MM_CHUNK_IS_ARENA_TAG(t) ((t) > MM_CHUNK_IDX_TO_TAG(MM_CHUNK_ARENA_MAX))

typedef uint16_t mm_chunk_tag_t;

typedef void * (*mm_chunk_alloc_t)(mm_chunk_tag_t tag, size_t size);
typedef void (*mm_chunk_free_t)(mm_chunk_tag_t tag, void *chunk);

bool mm_chunk_is_private_alloc_ready(void);

void mm_chunk_set_private_alloc(mm_chunk_alloc_t alloc, mm_chunk_free_t free)
	__attribute__((nonnull(1, 2)));

mm_chunk_tag_t mm_chunk_add_arena(mm_arena_t arena)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Chunk Access.
 **********************************************************************/

#define MM_CHUNK_OVERHEAD (sizeof(struct mm_chunk) + MM_ALLOC_OVERHEAD)

struct mm_chunk_base
{
	struct mm_link link;
	mm_chunk_tag_t tag;
};

/* A chunk of memory that could be chained together with other chunks and
   passed from one thread to another. Useful for I/O buffers and such. */
struct mm_chunk
{
	struct mm_chunk_base base;
	char data[];
};

static inline mm_chunk_tag_t
mm_chunk_base_gettag(const struct mm_chunk_base *chunk)
{
	return chunk->tag;
}

static inline mm_chunk_tag_t
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

struct mm_chunk * mm_chunk_create(mm_chunk_tag_t type, size_t size);

void mm_chunk_destroy(struct mm_chunk *chunk)
	__attribute__((nonnull(1)));

void mm_chunk_destroy_chain(struct mm_link *link);

#endif /* BASE_MEM_CHUNK_H */
