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
	struct mm_chunk *chunk = mm_core_alloc(total_size);
	chunk->size = size;
	chunk->used = 0;
	chunk->core = mm_core_self();
	chunk->next = NULL;
	return chunk;
}

void
mm_chunk_destroy(struct mm_chunk *chunk)
{
	ASSERT(chunk->core == mm_core_self());

	mm_core_free(chunk);
}

void
mm_chunk_destroy_chain(struct mm_chunk *chunk)
{
	ENTER();

	while (chunk != NULL) {
		struct mm_chunk *next = chunk->next;
		mm_chunk_destroy(chunk);
		chunk = next;
	}

	LEAVE();
}
