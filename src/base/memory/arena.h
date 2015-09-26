/*
 * base/memory/arena.h - MainMemory memory arenas.
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

#ifndef BASE_MEMORY_ARENA_H
#define BASE_MEMORY_ARENA_H

#include "common.h"

/**********************************************************************
 * Abstract Memory Arena.
 **********************************************************************/

#define MM_ARENA_VTABLE(name, a, ca, rea, f)			\
	static const struct mm_arena_vtable name = {		\
		.alloc = (mm_arena_alloc_t) a,			\
		.calloc = (mm_arena_calloc_t) ca,		\
		.realloc = (mm_arena_realloc_t) rea,		\
		.free = (mm_arena_free_t) f,			\
	}

typedef const struct mm_arena *mm_arena_t;

typedef void * (*const mm_arena_alloc_t)(mm_arena_t arena, size_t size);
typedef void * (*const mm_arena_calloc_t)(mm_arena_t arena, size_t count, size_t size);
typedef void * (*const mm_arena_realloc_t)(mm_arena_t arena, void *ptr, size_t size);
typedef void (*const mm_arena_free_t)(mm_arena_t arena, void *ptr);

struct mm_arena_vtable
{
	mm_arena_alloc_t alloc;
	mm_arena_calloc_t calloc;
	mm_arena_realloc_t realloc;
	mm_arena_free_t free;
};

struct mm_arena
{
	const struct mm_arena_vtable *vtable;
};

static inline void *
mm_arena_alloc(mm_arena_t arena, size_t size)
{
	return (arena->vtable->alloc)(arena, size);
}

static inline void *
mm_arena_calloc(mm_arena_t arena, size_t count, size_t size)
{
	return (arena->vtable->calloc)(arena, count, size);
}

static inline void *
mm_arena_realloc(mm_arena_t arena, void *ptr, size_t size)
{
	return (arena->vtable->realloc)(arena, ptr, size);
}

static inline void
mm_arena_free(mm_arena_t arena, void *ptr)
{
	(arena->vtable->free)(arena, ptr);
}

static inline void *
mm_arena_memdup(mm_arena_t arena, const void *ptr, size_t size)
{
	return memcpy(mm_arena_alloc(arena, size), ptr, size);
}

static inline char *
mm_arena_strdup(mm_arena_t arena, const char *ptr)
{
	return mm_arena_memdup(arena, ptr, strlen(ptr) + 1);
}

/**********************************************************************
 * Global Memory Arena.
 **********************************************************************/

/*
 * The global memory allocation routines should only be used to create
 * key global data structures during system bootstrap. After bootstrap
 * memory allocation should be done with dedicated spaces.
 */

extern const struct mm_arena mm_global_arena;

#endif /* BASE_MEMORY_ARENA_H */
