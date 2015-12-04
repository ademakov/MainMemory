/*
 * base/memory/global.c - MainMemory global memory allocation routines.
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

#include "base/memory/global.h"
#include "base/memory/malloc.h"

#include "base/lock.h"
#include "base/log/error.h"

/**********************************************************************
 * Basic global memory allocation routines.
 **********************************************************************/

static mm_lock_t mm_global_alloc_lock = MM_LOCK_INIT;

void *
mm_global_alloc(size_t size)
{
	mm_global_lock(&mm_global_alloc_lock);
	void *ptr = dlmalloc(size);
	mm_global_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_global_aligned_alloc(size_t align, size_t size)
{
	mm_global_lock(&mm_global_alloc_lock);
	void *ptr = dlmemalign(align, size);
	mm_global_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_global_calloc(size_t count, size_t size)
{
	mm_global_lock(&mm_global_alloc_lock);
	void *ptr = dlcalloc(count, size);
	mm_global_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	return ptr;
}

void *
mm_global_realloc(void *ptr, size_t size)
{
	mm_global_lock(&mm_global_alloc_lock);
	ptr = dlrealloc(ptr, size);
	mm_global_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_global_free(void *ptr)
{
	mm_global_lock(&mm_global_alloc_lock);
	dlfree(ptr);
	mm_global_unlock(&mm_global_alloc_lock);
}

size_t
mm_global_getallocsize(const void *ptr)
{
	return dlmalloc_usable_size(ptr);
}

/**********************************************************************
 * Global memory arena.
 **********************************************************************/

static void *
mm_global_arena_alloc(const struct mm_arena *arena UNUSED, size_t size)
{
	return mm_global_alloc(size);
}
static void *
mm_global_arena_calloc(const struct mm_arena *arena UNUSED, size_t count, size_t size)
{
	return mm_global_calloc(count, size);
}
static void *
mm_global_arena_realloc(const struct mm_arena *arena UNUSED, void *ptr, size_t size)
{
	return mm_global_realloc(ptr, size);
}
static void
mm_global_arena_free(const struct mm_arena *arena UNUSED, void *ptr)
{
	mm_global_free(ptr);
}

MM_ARENA_VTABLE(mm_global_arena_vtable,
	mm_global_arena_alloc,
	mm_global_arena_calloc,
	mm_global_arena_realloc,
	mm_global_arena_free);

const struct mm_arena mm_global_arena = { .vtable = &mm_global_arena_vtable };
