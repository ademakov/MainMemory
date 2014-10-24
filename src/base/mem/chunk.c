/*
 * base/mem/chunk.c - MainMemory chunks.
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

#include "base/mem/chunk.h"
#include "base/lock.h"
#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/mem/space.h"

/**********************************************************************
 * Chunk Tags.
 **********************************************************************/

static mm_arena_t mm_chunk_arena_table[MM_CHUNK_ARENA_MAX] = {
	&mm_global_arena,
	&mm_common_space.arena,
};

static int mm_chunk_arena_count = 2;

static mm_chunk_alloc_t mm_chunk_alloc = NULL;
static mm_chunk_free_t mm_chunk_free = NULL;

static mm_lock_t mm_chunk_lock = MM_LOCK_INIT;

bool
mm_chunk_is_special_alloc_ready(void)
{
	return (mm_chunk_alloc != NULL);
}

void
mm_chunk_set_special_alloc(mm_chunk_alloc_t alloc, mm_chunk_free_t free)
{
	mm_global_lock(&mm_chunk_lock);

	// If chunk allocation routines are replaced when there are already
	// some allocated chunks then it might explode on freeing them.
	if (mm_chunk_alloc != NULL || mm_chunk_free != NULL)
		mm_fatal(0, "special chunk allocation might only be initialized once");

	mm_chunk_alloc = alloc;
	mm_chunk_free = free;

	mm_global_unlock(&mm_chunk_lock);
}

mm_chunk_tag_t
mm_chunk_add_arena(mm_arena_t arena)
{
	mm_global_lock(&mm_chunk_lock);

	if (mm_chunk_arena_count == MM_CHUNK_ARENA_MAX) {
		mm_global_unlock(&mm_chunk_lock);
		mm_fatal(0, "too many chunk allocation arenas");
	}

	int idx = mm_chunk_arena_count++;
	mm_chunk_arena_table[idx] = arena;

	mm_global_unlock(&mm_chunk_lock);

	return MM_CHUNK_IDX_TO_TAG(idx);
}

/**********************************************************************
 * Chunk Creation and Destruction.
 **********************************************************************/

struct mm_chunk *
mm_chunk_create(mm_chunk_tag_t tag, size_t size)
{
	size += sizeof(struct mm_chunk);

	struct mm_chunk *chunk;
	if (MM_CHUNK_IS_ARENA_TAG(tag)) {
		int idx = MM_CHUNK_TAG_TO_IDX(tag);
		mm_arena_t arena = mm_chunk_arena_table[idx];
		if (unlikely(arena == NULL))
			mm_fatal(0, "chunk allocation arena is not initialized");
		chunk = mm_arena_alloc(arena, size);
	} else {
		if (unlikely(mm_chunk_alloc == NULL))
			mm_fatal(0, "special chunk allocation is not initialized");
		chunk = mm_chunk_alloc(tag, size);
	}

	chunk->base.tag = tag;
	mm_link_init(&chunk->base.link);

	return chunk;
}

void
mm_chunk_destroy(struct mm_chunk *chunk)
{
	mm_chunk_tag_t tag = mm_chunk_gettag(chunk);

	if (MM_CHUNK_IS_ARENA_TAG(tag)) {
		int idx = MM_CHUNK_TAG_TO_IDX(tag);
		mm_arena_t arena = mm_chunk_arena_table[idx];
		if (unlikely(arena == NULL))
			mm_fatal(0, "chunk allocation arena is not initialized");
		mm_arena_free(arena, chunk);
	} else {
		if (unlikely(mm_chunk_free == NULL))
			mm_fatal(0, "special chunk allocation is not initialized");
		mm_chunk_free(tag, chunk);
	}
}

void
mm_chunk_destroy_chain(struct mm_link *link)
{
	while (link != NULL) {
		struct mm_link *next = link->next;
		struct mm_chunk *chunk = containerof(link, struct mm_chunk, base.link);
		mm_chunk_destroy(chunk);
		link = next;
	}
}