/*
 * base/memory/region.h - MainMemory region allocator.
 *
 * Copyright (C) 2015-2016  Aleksey Demakov
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

#ifndef BASE_MEMORY_REGION_H
#define BASE_MEMORY_REGION_H

#include "common.h"
#include "base/bitops.h"
#include "base/log/debug.h"
#include "base/memory/chunk.h"

#define MM_REGION_ALIGN		(sizeof(uintptr_t))
#define MM_REGION_CHUNK_SIZE	(4 * 1024 - MM_CHUNK_OVERHEAD)

struct mm_region
{
	/* The last allocated memory block. */
	char *block_ptr;
	/* The last allocated memory block's end. */
	char *block_end;
	/* The currently used memory chunk's end. */
	char *chunk_end;
	/* Entire region memory as a list of chunks. */
	struct mm_stack chunks;
};

void NONNULL(1)
mm_region_prepare(struct mm_region *reg);

void NONNULL(1)
mm_region_cleanup(struct mm_region *reg);

void NONNULL(1)
mm_region_reserve(struct mm_region *reg, size_t more_size);

static inline size_t
mm_region_round_size(size_t size)
{
	return mm_round_up(size, MM_REGION_ALIGN);
}

static inline bool NONNULL(1)
mm_region_empty(struct mm_region *reg)
{
	return (reg->block_ptr == NULL);
}

static inline bool NONNULL(1)
mm_region_whole(struct mm_region *reg)
{
	return (reg->block_ptr == reg->block_end);
}

static inline size_t NONNULL(1)
mm_region_getroom(struct mm_region *reg)
{
	return (reg->chunk_end - reg->block_end);
}

static inline size_t NONNULL(1)
mm_region_getblocksize(struct mm_region *reg)
{
	return (reg->block_end - reg->block_ptr);
}

static inline void * NONNULL(1)
mm_region_getblockbase(struct mm_region *reg)
{
	return reg->block_ptr;
}

/*
 * Incrementally allocate a memory block without checking if there is enough
 * memory room for it. Returns a pointer to the additional memory.
 */
static inline void * NONNULL(1)
mm_region_extend_fast(struct mm_region *reg, size_t size)
{
	char *end = reg->block_end;
	reg->block_end += size;
	return end;
}

/*
 * Incrementally allocate a memory block and make sure the current memory
 * chunk is large enough to accommodate the requested size increment.
 * Returns a pointer to the additional memory.
 */
static inline void * NONNULL(1)
mm_region_extend(struct mm_region *reg, size_t size)
{
	if (mm_region_getroom(reg) < size)
		mm_region_reserve(reg, size);
	return mm_region_extend_fast(reg, size);
}

/* Finalize an incrementally allocated block. */
static inline void * NONNULL(1)
mm_region_finish(struct mm_region *reg)
{
	/* For a block with zero size force a minimal allocation. */
	if (unlikely(reg->block_ptr == reg->block_end)) {
		if (unlikely(reg->block_end == reg->chunk_end))
			mm_region_reserve(reg, 1);
		reg->block_end++;
	}

	/* Ensure a proper alignment of the next allocated block
	   rounding the size of the current one. */
	uintptr_t addr = (uintptr_t) reg->block_end;
	addr = mm_round_up(addr, MM_REGION_ALIGN);
	reg->block_end = (char *) addr;

	/* But beware of running over the chunk boundary. */
	if (unlikely(reg->block_end > reg->chunk_end))
		reg->block_end = reg->chunk_end;

	/* Finally move past the block and return a pointer to its start. */
	char *ptr = reg->block_ptr;
	reg->block_ptr = reg->block_end;
	return ptr;
}

/* Allocate at once a whole memory block. */
static inline void * NONNULL(1)
mm_region_alloc(struct mm_region *reg, size_t size)
{
	ASSERT(mm_region_whole(reg));
	mm_region_extend(reg, size);
	return mm_region_finish(reg);
}

static inline void * NONNULL(1)
mm_region_memdup(struct mm_region *reg, const void *ptr, size_t size)
{
	return memcpy(mm_region_alloc(reg, size), ptr, size);
}

static inline char * NONNULL(1)
mm_region_strdup(struct mm_region *reg, const char *ptr)
{
	return mm_region_memdup(reg, ptr, strlen(ptr) + 1);
}

#endif /* BASE_MEMORY_REGION_H */
