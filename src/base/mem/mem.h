/*
 * base/mem/mem.h - MainMemory memory subsystem.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#ifndef BASE_MEM_MEM_H
#define BASE_MEM_MEM_H

#include "base/mem/chunk.h"

void mm_memory_init(mm_chunk_alloc_t alloc, mm_chunk_free_t free)
	__attribute__((nonnull(1, 2)));

void mm_memory_term(void);

#endif /* BASE_MEM_MEM_H */
