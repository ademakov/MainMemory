/*
 * chunk.c - MainMemory chunks.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#include "chunk.h"

#include "alloc.h"
#include "core.h"
#include "util.h"

struct mm_chunk *
mm_chunk_create(size_t size)
{
	ENTER();

	size_t total_size = sizeof(struct mm_chunk) + size;
	struct mm_chunk *chunk = mm_core_alloc(total_size);
	chunk->size = size;
	chunk->used = 0;
	chunk->core = mm_core;

	LEAVE();
	return chunk;
}

void
mm_chunk_destroy(struct mm_chunk *chunk)
{
	ENTER();
	ASSERT(chunk->core == mm_core);

	mm_core_free(chunk);

	LEAVE();
}

void
mm_chunk_destroy_global(struct mm_chunk *chunk)
{
	ENTER();

	if (chunk->core == mm_core) {
		mm_chunk_destroy(chunk);
	} else {
		mm_global_lock(&chunk->core->chunks_lock);
		mm_list_insert(&chunk->core->chunks, &chunk->link);
		mm_global_unlock(&chunk->core->chunks_lock);
	}

	LEAVE();
}

void
mm_chunk_destroy_chain(struct mm_list *head, struct mm_list *tail)
{
	ENTER();

	for (;;) {
		struct mm_chunk *chunk = containerof(head, struct mm_chunk, link);
		if (head != tail) {
			head = head->next;
			mm_chunk_destroy(chunk);
		} else {
			mm_chunk_destroy(chunk);
			break;
		}
	}

	LEAVE();
}

void
mm_chunk_destroy_chain_global(struct mm_list *head, struct mm_list *tail)
{
	ENTER();

	struct mm_chunk *chunk = containerof(head, struct mm_chunk, link);

#if ENABLE_DEBUG
	struct mm_list *next = head;
	while (next != tail) {
		next = next->next;
		struct mm_chunk *chunk2 = containerof(next, struct mm_chunk, link);
		ASSERT(chunk2->core == chunk->core);
	}
#endif

	if (chunk->core == mm_core) {
		mm_chunk_destroy_chain(head, tail);
	} else {
		mm_global_lock(&chunk->core->chunks_lock);
		mm_list_splice_next(&chunk->core->chunks, head, tail);
		mm_global_unlock(&chunk->core->chunks_lock);
	}

	LEAVE();
}
