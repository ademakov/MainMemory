/*
 * alloc.c - MainMemory memory allocation.
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

#include "alloc.h"

#include "core.h"
#include "lock.h"
#include "util.h"

#include "dlmalloc/malloc.h"

/**********************************************************************
 * Stubs for LIBC Memory Allocation Routines.
 **********************************************************************/

void *
malloc(size_t size)
{
	mm_print("who still needs malloc?");
	return mm_alloc(size);
}

void *
calloc(size_t count, size_t size)
{
	mm_print("who still needs calloc?");
	return mm_calloc(count, size);
}

void
free(void *ptr)
{
	mm_print("who still needs free?");
	mm_free(ptr);
}


/**********************************************************************
 * Memory Allocation for Core Threads.
 **********************************************************************/

void *
mm_core_alloc(size_t size)
{
	void *ptr = mspace_malloc(mm_core->arena, size);

	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	}
	return ptr;
}

void
mm_core_free(void *ptr)
{
	mspace_free(mm_core->arena, ptr);
}

/**********************************************************************
 * Memory Allocation Routines.
 **********************************************************************/

static mm_global_lock_t mm_alloc_lock = MM_ATOMIC_LOCK_INIT;

void *
mm_alloc(size_t size)
{
	mm_global_lock(&mm_alloc_lock);
	void *ptr = dlmalloc(size);
	mm_global_unlock(&mm_alloc_lock);

	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	}
	return ptr;
}

void *
mm_realloc(void *ptr, size_t size)
{
	mm_global_lock(&mm_alloc_lock);
	ptr = dlrealloc(ptr, size);
	mm_global_unlock(&mm_alloc_lock);

	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	}
	return ptr;
}

void *
mm_calloc(size_t count, size_t size)
{
	mm_global_lock(&mm_alloc_lock);
	void *ptr = dlcalloc(count, size);
	mm_global_unlock(&mm_alloc_lock);

	if (unlikely(ptr == NULL)) {
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	}
	return ptr;
}

void
mm_free(void *ptr)
{
	mm_global_lock(&mm_alloc_lock);
	dlfree(ptr);
	mm_global_unlock(&mm_alloc_lock);
}

