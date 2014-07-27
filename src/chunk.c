/*
 * chunk.c - MainMemory chunks.
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

#include "chunk.h"

#include "alloc.h"
#include "core.h"
#include "trace.h"

struct mm_chunk *
mm_chunk_create(size_t size)
{
	size_t total_size = sizeof(struct mm_chunk) + size;
	struct mm_chunk *chunk = mm_local_alloc(total_size);
	chunk->used = 0;
	chunk->core = mm_core_selfid();
	mm_link_init(&chunk->link);
	return chunk;
}

void
mm_chunk_destroy(struct mm_chunk *chunk)
{
	ASSERT(chunk->core != MM_CORE_NONE);
	ASSERT(chunk->core == mm_core_selfid());

	mm_local_free(chunk);
}

void
mm_chunk_destroy_chain(struct mm_chunk *chunk)
{
	ENTER();

	if (chunk != NULL) {
		for (;;) {
			struct mm_link *link = chunk->link.next;
			mm_chunk_destroy(chunk);
			if (link == NULL)
				break;
			chunk = containerof(link, struct mm_chunk, link);
		}
	}

	LEAVE();
}

struct mm_chunk *
mm_chunk_create_global(size_t size)
{
	size_t total_size = sizeof(struct mm_chunk) + size;
	struct mm_chunk *chunk = mm_global_alloc(total_size);
	chunk->used = 0;
	chunk->core = MM_CORE_NONE;
	mm_link_init(&chunk->link);
	return chunk;
}

void
mm_chunk_destroy_global(struct mm_chunk *chunk)
{
	ASSERT(chunk->core == MM_CORE_NONE);

	mm_global_free(chunk);
}

void
mm_chunk_destroy_chain_global(struct mm_chunk *chunk)
{
	ENTER();

	if (chunk != NULL) {
		for (;;) {
			struct mm_link *link = chunk->link.next;
			mm_chunk_destroy_global(chunk);
			if (link == NULL)
				break;
			chunk = containerof(link, struct mm_chunk, link);
		}
	}

	LEAVE();
}
