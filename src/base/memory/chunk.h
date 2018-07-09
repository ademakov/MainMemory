/*
 * base/memory/chunk.h - MainMemory memory chunks.
 *
 * Copyright (C) 2013-2018  Aleksey Demakov
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

#ifndef BASE_MEMORY_CHUNK_H
#define BASE_MEMORY_CHUNK_H

#include "common.h"
#include "base/list.h"
#include "base/memory/alloc.h"
#include "base/memory/space.h"

struct mm_thread;

/**********************************************************************
 * Chunk tags.
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
 * Chunk access.
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

static inline mm_chunk_t NONNULL(1)
mm_chunk_base_gettag(const struct mm_chunk_base *chunk)
{
	return chunk->tag;
}

static inline mm_chunk_t NONNULL(1)
mm_chunk_gettag(const struct mm_chunk *chunk)
{
	return mm_chunk_base_gettag(&chunk->base);
}

static inline size_t NONNULL(1)
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

static inline size_t NONNULL(1)
mm_chunk_getsize(const struct mm_chunk *chunk)
{
	return mm_chunk_base_getsize(&chunk->base);
}

static inline struct mm_chunk *
mm_chunk_from_slink(struct mm_slink *link)
{
#if 1
	return (struct mm_chunk *) link;
#else
	return link == NULL ? NULL : containerof(link, struct mm_chunk, base.slink);
#endif
}

static inline struct mm_chunk *
mm_chunk_from_qlink(struct mm_qlink *link)
{
#if 1
	return (struct mm_chunk *) link;
#else
	return link == NULL ? NULL : containerof(link, struct mm_chunk, base.qlink);
#endif
}

static inline struct mm_chunk * NONNULL(1)
mm_chunk_stack_head(struct mm_stack *stack)
{
	return mm_chunk_from_slink(mm_stack_head(stack));
}

static inline struct mm_chunk * NONNULL(1)
mm_chunk_stack_next(struct mm_chunk *chunk)
{
	return mm_chunk_from_slink(chunk->base.slink.next);
}

static inline void NONNULL(1, 2)
mm_chunk_stack_insert(struct mm_stack *stack, struct mm_chunk *chunk)
{
	mm_stack_insert(stack, &chunk->base.slink);
}

static inline struct mm_chunk * NONNULL(1)
mm_chunk_stack_remove(struct mm_stack *stack)
{
	return mm_chunk_from_slink(mm_stack_remove(stack));
}

static inline struct mm_chunk * NONNULL(1)
mm_chunk_queue_head(struct mm_queue *queue)
{
	return mm_chunk_from_qlink(mm_queue_head(queue));
}

static inline struct mm_chunk * NONNULL(1)
mm_chunk_queue_tail(struct mm_queue *queue)
{
	return mm_chunk_from_qlink(mm_queue_tail(queue));
}

static inline struct mm_chunk * NONNULL(1)
mm_chunk_queue_next(struct mm_chunk *chunk)
{
	return mm_chunk_from_qlink(chunk->base.qlink.next);
}

static inline void NONNULL(1, 2)
mm_chunk_queue_append(struct mm_queue *queue, struct mm_chunk *chunk)
{
	mm_queue_append(queue, &chunk->base.qlink);
}

static inline void NONNULL(1, 2)
mm_chunk_queue_prepend(struct mm_queue *queue, struct mm_chunk *chunk)
{
	mm_queue_prepend(queue, &chunk->base.qlink);
}

static inline struct mm_chunk * NONNULL(1)
mm_chunk_queue_remove(struct mm_queue *queue)
{
	return mm_chunk_from_qlink(mm_queue_remove(queue));
}

/**********************************************************************
 * Chunk creation and destruction.
 **********************************************************************/

struct mm_chunk * MALLOC
mm_chunk_create_global(size_t size);

struct mm_chunk * MALLOC
mm_chunk_create_common(size_t size);

struct mm_chunk * MALLOC
mm_chunk_create_regular(size_t size);

struct mm_chunk * MALLOC
mm_chunk_create_private(size_t size);

struct mm_chunk * MALLOC
mm_chunk_create(size_t size);

void NONNULL(1)
mm_chunk_destroy(struct mm_chunk *chunk);

void NONNULL(1)
mm_chunk_destroy_stack(struct mm_stack *stack);

void NONNULL(1)
mm_chunk_destroy_queue(struct mm_queue *queue);

void NONNULL(1)
mm_chunk_enqueue_deferred(struct mm_thread *thread, bool flush);

#endif /* BASE_MEMORY_CHUNK_H */
