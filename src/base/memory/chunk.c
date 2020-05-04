/*
 * base/memory/chunk.c - MainMemory chunks.
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

#include "base/memory/chunk.h"

#include "base/async.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/thread/backoff.h"
#include "base/thread/thread.h"

#define MM_CHUNK_FLUSH_THRESHOLD	(64)
#define MM_CHUNK_ERROR_THRESHOLD	(512)
#define MM_CHUNK_FATAL_THRESHOLD	(4096)

/**********************************************************************
 * Chunk Allocation and Reclamation.
 **********************************************************************/

struct mm_chunk * MALLOC
mm_chunk_create(size_t size)
{
	size += sizeof(struct mm_chunk);
	struct mm_chunk *chunk = mm_memory_alloc(size);
	mm_slink_prepare(&chunk->base.slink);
	return chunk;
}

void NONNULL(1)
mm_chunk_destroy_stack(struct mm_stack *stack)
{
	struct mm_chunk *chunk = mm_chunk_stack_head(stack);
	while (chunk != NULL) {
		struct mm_chunk *next = mm_chunk_stack_next(chunk);
		mm_chunk_destroy(chunk);
		chunk = next;
	}
}

void NONNULL(1)
mm_chunk_destroy_queue(struct mm_queue *queue)
{
	struct mm_chunk *chunk = mm_chunk_queue_head(queue);
	while (chunk != NULL) {
		struct mm_chunk *next = mm_chunk_queue_next(chunk);
		mm_chunk_destroy(chunk);
		chunk = next;
	}
}
