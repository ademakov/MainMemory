/*
 * base/memory/global.h - MainMemory global memory allocation routines.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#ifndef BASE_MEMORY_GLOBAL_H
#define BASE_MEMORY_GLOBAL_H

#include "common.h"
#include "base/memory/arena.h"

/*
 * The global memory allocation routines should only be used to create
 * a few key global data structures during the system bootstrap. After
 * the bootstrap memory allocation should be done with dedicated spaces.
 */

/**********************************************************************
 * Basic global memory allocation routines.
 **********************************************************************/

void * MALLOC
mm_global_alloc(size_t size);

void * MALLOC
mm_global_aligned_alloc(size_t align, size_t size);

void * MALLOC
mm_global_calloc(size_t count, size_t size);

void *
mm_global_realloc(void *ptr, size_t size);

void
mm_global_free(void *ptr);

size_t
mm_global_getallocsize(const void *ptr);

/**********************************************************************
 * Auxiliary global memory allocation routines.
 **********************************************************************/

static inline void *
mm_global_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_global_alloc(size), ptr, size);
}

static inline char *
mm_global_strdup(const char *ptr)
{
	return mm_global_memdup(ptr, strlen(ptr) + 1);
}

/**********************************************************************
 * Global memory arena.
 **********************************************************************/

extern const struct mm_arena mm_global_arena;

#endif /* BASE_MEMORY_GLOBAL_H */
