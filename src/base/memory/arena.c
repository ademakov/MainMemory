/*
 * base/memory/arena.c - MainMemory memory arenas.
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

#include "base/memory/arena.h"
#include "base/memory/alloc.h"

/**********************************************************************
 * Global Memory Arena.
 **********************************************************************/

static void *
mm_global_arena_alloc(const struct mm_arena *arena __mm_unused__,
		      size_t size)
{
	return mm_global_alloc(size);
}
static void *
mm_global_arena_calloc(const struct mm_arena *arena __mm_unused__,
		       size_t count, size_t size)
{
	return mm_global_calloc(count, size);
}
static void *
mm_global_arena_realloc(const struct mm_arena *arena __mm_unused__,
			void *ptr, size_t size)
{
	return mm_global_realloc(ptr, size);
}
static void
mm_global_arena_free(const struct mm_arena *arena __mm_unused__,
		     void *ptr)
{
	mm_global_free(ptr);
}

MM_ARENA_VTABLE(mm_global_arena_vtable,
	mm_global_arena_alloc,
	mm_global_arena_calloc,
	mm_global_arena_realloc,
	mm_global_arena_free);

const struct mm_arena mm_global_arena = { .vtable = &mm_global_arena_vtable };
