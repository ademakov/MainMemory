/*
 * base/memory/alloc.h - MainMemory context-aware memory allocation.
 *
 * Copyright (C) 2020  Aleksey Demakov
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

#ifndef BASE_MEMORY_ALLOC_H
#define BASE_MEMORY_ALLOC_H

#include "common.h"

/**********************************************************************
 * Basic memory allocation routines.
 **********************************************************************/

void * MALLOC
mm_memory_alloc(size_t size);

void * MALLOC
mm_memory_zalloc(size_t size);

void * MALLOC
mm_memory_aligned_alloc(size_t align, size_t size);

void * MALLOC
mm_memory_calloc(size_t count, size_t size);

void * MALLOC
mm_memory_realloc(void *ptr, size_t size);

void * MALLOC
mm_memory_xalloc(size_t size);

void * MALLOC
mm_memory_xzalloc(size_t size);

void * MALLOC
mm_memory_aligned_xalloc(size_t align, size_t size);

void * MALLOC
mm_memory_xcalloc(size_t count, size_t size);

void * MALLOC
mm_memory_xrealloc(void *ptr, size_t size);

void
mm_memory_free(void *ptr);

/**********************************************************************
 * Auxiliary memory allocation routines.
 **********************************************************************/

static inline void *
mm_memory_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_memory_xalloc(size), ptr, size);
}

static inline char *
mm_memory_strdup(const char *ptr)
{
	return mm_memory_memdup(ptr, strlen(ptr) + 1);
}

#endif /* BASE_MEMORY_ALLOC_H */
