/*
 * alloc.h - MainMemory memory allocation.
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

#ifndef ALLOC_H
#define ALLOC_H

#include "common.h"

/* DLMalloc overhead. */
#if MM_WORD_32BIT
# ifdef FOOTERS
#  define MM_ALLOC_OVERHEAD (4)
# else
#  define MM_ALLOC_OVERHEAD (8)
# endif
#else
# ifdef FOOTERS
#  define MM_ALLOC_OVERHEAD (8)
# else
#  define MM_ALLOC_OVERHEAD (16)
# endif
#endif

struct mm_allocator
{
	void * (*alloc)(size_t size);
	void * (*realloc)(void *ptr, size_t size);
	void (*free)(void *ptr);
};

extern const struct mm_allocator mm_alloc_core;
extern const struct mm_allocator mm_alloc_shared;
extern const struct mm_allocator mm_alloc_global;

void mm_alloc_init(void);
void mm_alloc_term(void);

/**********************************************************************
 * Intra-core memory allocation routines.
 **********************************************************************/

void * mm_core_alloc(size_t size)
	__attribute__((malloc));

void * mm_core_alloc_aligned(size_t align, size_t size)
	__attribute__((malloc));

void * mm_core_calloc(size_t count, size_t size)
	__attribute__((malloc));

void * mm_core_realloc(void *ptr, size_t size);

void mm_core_free(void *ptr);

/**********************************************************************
 * Cross-core memory allocation routines.
 **********************************************************************/

void * mm_shared_alloc(size_t size)
	__attribute__((malloc));

void * mm_shared_alloc_aligned(size_t align, size_t size)
	__attribute__((malloc));

void * mm_shared_calloc(size_t count, size_t size)
	__attribute__((malloc));

void * mm_shared_realloc(void *ptr, size_t size);

void mm_shared_free(void *ptr);

/**********************************************************************
 * Global memory allocation routines.
 **********************************************************************/

void * mm_global_alloc(size_t size)
	__attribute__((malloc));

void * mm_global_alloc_aligned(size_t align, size_t size)
	__attribute__((malloc));

void * mm_global_calloc(size_t count, size_t size)
	__attribute__((malloc));

void * mm_global_realloc(void *ptr, size_t size);

void mm_global_free(void *ptr);

#endif /* ALLOC_H */
