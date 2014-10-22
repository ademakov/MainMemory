/*
 * base/mem/alloc.h - MainMemory memory allocation.
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

#ifndef BASE_MEM_ALLOC_H
#define BASE_MEM_ALLOC_H

#include "common.h"

/* DLMalloc overhead. */
#if MM_WORD_32BIT
# ifndef FOOTERS
#  define MM_ALLOC_OVERHEAD (4)
# else
#  define MM_ALLOC_OVERHEAD (8)
# endif
#else
# ifndef FOOTERS
#  define MM_ALLOC_OVERHEAD (8)
# else
#  define MM_ALLOC_OVERHEAD (16)
# endif
#endif

/**********************************************************************
 * Memory subsystem initialization and termination.
 **********************************************************************/

void mm_alloc_init(void);
void mm_alloc_term(void);

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

void * mm_shared_memdup(const void *ptr, size_t size)
	__attribute__((malloc));

char * mm_shared_strdup(const char *ptr)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void mm_shared_free(void *ptr);

size_t mm_shared_alloc_size(const void *ptr);

/**********************************************************************
 * Global Memory Allocation Routines.
 **********************************************************************/

/*
 * The global memory allocation functions should only be used to create
 * few key global data structures.
 */

void * mm_global_alloc(size_t size)
	__attribute__((malloc));

void * mm_global_aligned_alloc(size_t align, size_t size)
	__attribute__((malloc));

void * mm_global_calloc(size_t count, size_t size)
	__attribute__((malloc));

void * mm_global_realloc(void *ptr, size_t size);

void * mm_global_memdup(const void *ptr, size_t size)
	__attribute__((malloc));

char * mm_global_strdup(const char *ptr)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void mm_global_free(void *ptr);

size_t mm_global_alloc_size(const void *ptr);

/**********************************************************************
 * Memory Space Allocation Routines.
 **********************************************************************/

typedef void * mm_mspace_t;

mm_mspace_t mm_mspace_create(void);

void mm_mspace_destroy(mm_mspace_t space);

void * mm_mspace_alloc(mm_mspace_t space, size_t size)
	__attribute__((malloc));

void * mm_mspace_xalloc(mm_mspace_t space, size_t size)
	__attribute__((malloc));

void * mm_mspace_aligned_alloc(mm_mspace_t space, size_t align, size_t size)
	__attribute__((malloc));

void * mm_mspace_aligned_xalloc(mm_mspace_t space, size_t align, size_t size)
	__attribute__((malloc));

void * mm_mspace_calloc(mm_mspace_t space, size_t count, size_t size)
	__attribute__((malloc));

void * mm_mspace_xcalloc(mm_mspace_t space, size_t count, size_t size)
	__attribute__((malloc));

void * mm_mspace_realloc(mm_mspace_t space, void *ptr, size_t size);

void * mm_mspace_xrealloc(mm_mspace_t space, void *ptr, size_t size);

void mm_mspace_free(mm_mspace_t space, void *ptr);

size_t mm_mspace_getfootprint(mm_mspace_t space);

size_t mm_mspace_getfootprint_limit(mm_mspace_t space);
size_t mm_mspace_setfootprint_limit(mm_mspace_t space, size_t size);

size_t mm_mspace_getallocsize(const void *ptr);

/**********************************************************************
 * Simple Memory Arenas.
 **********************************************************************/

extern const struct mm_arena mm_shared_arena;
extern const struct mm_arena mm_global_arena;

#endif /* BASE_MEM_ALLOC_H */
