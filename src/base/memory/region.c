/*
 * base/memory/region.c - MainMemory region allocator.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "base/memory/region.h"

#include "base/log/error.h"
#include "base/log/trace.h"
#include "base/memory/memory.h"

void NONNULL(1)
mm_region_prepare(struct mm_region *reg)
{
	ENTER();

	reg->block_ptr = NULL;
	reg->block_end = NULL;
	reg->chunk_end = NULL;

	mm_stack_prepare(&reg->chunks);

	LEAVE();
}

void NONNULL(1)
mm_region_cleanup(struct mm_region *reg)
{
	ENTER();

	mm_chunk_destroy_stack(&reg->chunks);

	LEAVE();
}

void NONNULL(1)
mm_region_reserve(struct mm_region *reg, size_t more_size)
{
	ENTER();

	// Find out the required memory block size.
	size_t old_size = reg->block_end - reg->block_ptr;
	size_t new_size = old_size + more_size;

	// Find out the required memory chunk size. It has to fit the required
	// block plus a tiny bit for the initial block alignment, and provide
	// some extra room to amortize further allocation.
	size_t chunk_size = MM_REGION_CHUNK_SIZE;
	if (new_size > (3 * MM_REGION_CHUNK_SIZE / 4))
		chunk_size = new_size + new_size / 2;
	if (unlikely(chunk_size < more_size) || unlikely(chunk_size < old_size))
		mm_fatal(EOVERFLOW, "chunk size overflow");

	// Create a new memory chunk.
	struct mm_chunk *chunk = mm_chunk_create(new_size);
	reg->chunk_end = chunk->data + mm_chunk_getsize(chunk);
	mm_stack_insert(&reg->chunks, &chunk->base.slink);

	// Align the initial block address.
	uintptr_t addr = (uintptr_t) chunk->data;
	addr = mm_round_up(addr, MM_REGION_ALIGN);
	char *ptr = (char *) addr;

	// Copy the old block content if any.
	if (old_size) {
		memcpy(ptr, reg->block_ptr, old_size);

		// Free the old chunk if it was entirely used for the old block.
		struct mm_chunk *old_chunk = mm_chunk_from_slink(chunk->base.slink.next);
		addr = (uintptr_t) old_chunk->data;
		addr = mm_round_up(addr, MM_REGION_ALIGN);
		if (reg->block_ptr == (char *) addr) {
			mm_stack_remove_next(&chunk->base.slink);
			mm_chunk_destroy(old_chunk);
		}
	}

	reg->block_ptr = ptr;
	reg->block_end = ptr + old_size;

	LEAVE();
}
