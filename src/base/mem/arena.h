/*
 * mem/arena.h - MainMemory abstract memory arena.
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

#ifndef MEM_ARENA_H
#define MEM_ARENA_H

#include "common.h"

struct mm_arena
{
	const struct mm_arena_vtable *vtable;
};

typedef const struct mm_arena *mm_arena_t;

struct mm_arena_vtable
{
	void * (*const alloc)(mm_arena_t arena, size_t size);
	void * (*const calloc)(mm_arena_t arena, size_t count, size_t size);
	void * (*const realloc)(mm_arena_t arena, void *ptr, size_t size);
	void (*const free)(mm_arena_t arena, void *ptr);
};

static inline void *
mm_arena_alloc(mm_arena_t arena, size_t size)
{
	return (arena->vtable->alloc)(arena, size);
}

static inline void *
mm_arena_calloc(mm_arena_t arena, size_t count, ssize_t size)
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

#endif /* MEM_ARENA_H */
