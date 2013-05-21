/*
 * chunk.h - MainMemory chunks.
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

#ifndef CHUNK_H
#define CHUNK_H

#include "common.h"
#include "list.h"

/* A chunk of memory that could be chained together with other chunks and
   passed from one thread to another. Useful for I/O buffers and such. */
struct mm_chunk
{
	uint32_t used;
	uint32_t size;
	struct mm_list link;
	struct mm_core *core;
	char data[];
};

struct mm_chunk * mm_chunk_create(size_t size);

void mm_chunk_destroy(struct mm_chunk *chunk);
void mm_chunk_destroy_global(struct mm_chunk *chunk);

void mm_chunk_destroy_chain(struct mm_list *head, struct mm_list *tail);
void mm_chunk_destroy_chain_global(struct mm_list *head, struct mm_list *tail);

#endif /* CHUNK_H */
