/*
 * alloc.h - MainMemory memory allocation.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

struct mm_allocator
{
	void * (*alloc)(size_t size);
	void * (*realloc)(void *ptr, size_t size);
	void (*free)(void *ptr);
};

extern const struct mm_allocator mm_alloc_core;
extern const struct mm_allocator mm_alloc_global;

/**********************************************************************
 * Memory Allocation for Core Threads.
 **********************************************************************/

void * mm_core_alloc(size_t size)
	__attribute__((malloc));

void * mm_core_calloc(size_t count, size_t size)
	__attribute__((malloc));

void * mm_core_realloc(void *ptr, size_t size);

void mm_core_free(void *ptr);

/**********************************************************************
 * Global Memory Allocation Routines.
 **********************************************************************/

void * mm_alloc(size_t size)
	__attribute__((malloc));

void * mm_alloc_aligned(size_t align, size_t size)
	__attribute__((malloc));

void * mm_calloc(size_t count, size_t size)
	__attribute__((malloc));

void * mm_realloc(void *ptr, size_t size);

void mm_free(void *ptr);

#endif /* ALLOC_H */
