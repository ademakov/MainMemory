/*
 * base/memory/chunk.h - MainMemory memory chunks.
 *
 * Copyright (C) 2013-2020  Aleksey Demakov
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
#include "base/memory/cache.h"

struct mm_thread;

/**********************************************************************
 * Chunk access.
 **********************************************************************/

#define MM_CHUNK_OVERHEAD (sizeof(struct mm_chunk))

struct mm_chunk_base
{
	union
	{
		struct mm_slink slink;
		struct mm_qlink qlink;
	};
};

/* A chunk of memory that could be chained together with other chunks and
   passed from one thread to another. Useful for I/O buffers and such. */
struct mm_chunk
{
	struct mm_chunk_base base;
	char data[];
};

static inline size_t NONNULL(1)
mm_chunk_base_getsize(const struct mm_chunk_base *chunk)
{
	return mm_memory_cache_chunk_size(chunk) - sizeof(struct mm_chunk);
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
mm_chunk_create(size_t size);

static inline void NONNULL(1)
mm_chunk_destroy(struct mm_chunk *chunk)
{
	mm_memory_free(chunk);
}

void NONNULL(1)
mm_chunk_destroy_stack(struct mm_stack *stack);

void NONNULL(1)
mm_chunk_destroy_queue(struct mm_queue *queue);

#endif /* BASE_MEMORY_CHUNK_H */
