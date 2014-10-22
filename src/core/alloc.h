/*
 * core/alloc.h - MainMemory core and task specific memory allocation.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#ifndef CORE_ALLOC_H
#define CORE_ALLOC_H

#include "common.h"

/**********************************************************************
 * Cross-core allocator initialization and termination.
 **********************************************************************/

void mm_shared_alloc_init(void);
void mm_shared_alloc_term(void);

/**********************************************************************
 * Cross-core memory allocation routines.
 **********************************************************************/

void * mm_shared_alloc(size_t size)
	__attribute__((malloc));

void * mm_shared_aligned_alloc(size_t align, size_t size)
	__attribute__((malloc));

void * mm_shared_calloc(size_t count, size_t size)
	__attribute__((malloc));

void * mm_shared_realloc(void *ptr, size_t size);

void mm_shared_free(void *ptr);

static inline void *
mm_shared_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_shared_alloc(size), ptr, size);
}

static inline char *
mm_shared_strdup(const char *ptr)
{
	return mm_shared_memdup(ptr, strlen(ptr) + 1);
}

/**********************************************************************
 * Shared Memory Arena.
 **********************************************************************/

extern const struct mm_arena mm_shared_arena;

#endif /* CORE_ALLOC_H */
