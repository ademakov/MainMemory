/*
 * base/memory/arena.c - MainMemory memory arenas.
 *
 * Copyright (C) 2014-2020  Aleksey Demakov
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

static void *
mm_memory_arena_alloc(const struct mm_arena *arena UNUSED, size_t size)
{
	return mm_memory_alloc(size);
}
static void *
mm_memory_arena_calloc(const struct mm_arena *arena UNUSED, size_t count, size_t size)
{
	return mm_memory_calloc(count, size);
}
static void *
mm_memory_arena_realloc(const struct mm_arena *arena UNUSED, void *ptr, size_t size)
{
	return mm_memory_realloc(ptr, size);
}

static void *
mm_memory_arena_xalloc(const struct mm_arena *arena UNUSED, size_t size)
{
	return mm_memory_xalloc(size);
}
static void *
mm_memory_arena_xcalloc(const struct mm_arena *arena UNUSED, size_t count, size_t size)
{
	return mm_memory_calloc(count, size);
}
static void *
mm_memory_arena_xrealloc(const struct mm_arena *arena UNUSED, void *ptr, size_t size)
{
	return mm_memory_realloc(ptr, size);
}
static void
mm_memory_arena_free(const struct mm_arena *arena UNUSED, void *ptr)
{
	mm_memory_free(ptr);
}

MM_ARENA_VTABLE(mm_memory_uarena_vtable,
	mm_memory_arena_alloc,
	mm_memory_arena_calloc,
	mm_memory_arena_realloc,
	mm_memory_arena_free);

MM_ARENA_VTABLE(mm_memory_xarena_vtable,
	mm_memory_arena_xalloc,
	mm_memory_arena_xcalloc,
	mm_memory_arena_xrealloc,
	mm_memory_arena_free);

const struct mm_arena mm_memory_uarena = { .vtable = &mm_memory_uarena_vtable };
const struct mm_arena mm_memory_xarena = { .vtable = &mm_memory_xarena_vtable };
