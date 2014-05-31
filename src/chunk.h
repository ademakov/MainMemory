/*
 * chunk.h - MainMemory chunks.
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

#ifndef CHUNK_H
#define CHUNK_H

#include "common.h"
#include "alloc.h"
#include "list.h"
#include "trace.h"

#define MM_CHUNK_OVERHEAD (sizeof(struct mm_chunk) + MM_ALLOC_OVERHEAD)

/* A chunk of memory that could be chained together with other chunks and
   passed from one thread to another. Useful for I/O buffers and such. */
struct mm_chunk
{
	struct mm_link link;
	uint32_t used;
	mm_core_t core;
	char data[];
};

static inline size_t
mm_chunk_size(const struct mm_chunk *chunk)
{
	ASSERT(chunk->core != MM_CORE_NONE);
	return mm_local_alloc_size(chunk) - sizeof(struct mm_chunk);
}

static inline size_t
mm_chunk_size_global(const struct mm_chunk *chunk)
{
	ASSERT(chunk->core == MM_CORE_NONE);
	return mm_global_alloc_size(chunk) - sizeof(struct mm_chunk);
}

struct mm_chunk * mm_chunk_create(size_t size);

void mm_chunk_destroy(struct mm_chunk *chunk)
	__attribute__((nonnull(1)));

void mm_chunk_destroy_chain(struct mm_chunk *chunk);

struct mm_chunk * mm_chunk_create_global(size_t size);

void mm_chunk_destroy_global(struct mm_chunk *chunk)
	__attribute__((nonnull(1)));

void mm_chunk_destroy_chain_global(struct mm_chunk *chunk);

#endif /* CHUNK_H */
