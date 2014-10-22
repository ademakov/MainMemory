/*
 * core/alloc.c - MainMemory core and task specific memory allocation.
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

#include "core/alloc.h"
#include "core/lock.h"
#include "base/mem/alloc.h"
#include "base/mem/arena.h"
#include "base/log/error.h"

static mm_mspace_t mm_shared_space;
static mm_task_lock_t mm_shared_alloc_lock = MM_TASK_LOCK_INIT;

/**********************************************************************
 * Cross-core allocator initialization and termination.
 **********************************************************************/

void
mm_shared_alloc_init(void)
{
	mm_shared_space = mm_mspace_create();
}

void
mm_shared_alloc_term(void)
{
	mm_mspace_destroy(mm_shared_space);
}

/**********************************************************************
 * Cross-core memory allocation routines.
 **********************************************************************/

void *
mm_shared_alloc(size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	void *ptr = mm_mspace_alloc(mm_shared_space, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_shared_aligned_alloc(size_t align, size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	void *ptr = mm_mspace_aligned_alloc(mm_shared_space, align, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_shared_calloc(size_t count, size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	void *ptr = mm_mspace_calloc(mm_shared_space, count, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	return ptr;
}

void *
mm_shared_realloc(void *ptr, size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	ptr = mm_mspace_realloc(mm_shared_space, ptr, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_shared_free(void *ptr)
{
	mm_task_lock(&mm_shared_alloc_lock);
	mm_mspace_free(mm_shared_space, ptr);
	mm_task_unlock(&mm_shared_alloc_lock);
}

/**********************************************************************
 * Shared Memory Arena.
 **********************************************************************/

static void *
mm_shared_arena_alloc(const struct mm_arena *arena __attribute__((unused)),
		      size_t size)
{
	return mm_shared_alloc(size);
}
static void *
mm_shared_arena_calloc(const struct mm_arena *arena __attribute__((unused)),
		       size_t count, size_t size)
{
	return mm_shared_calloc(count, size);
}
static void *
mm_shared_arena_realloc(const struct mm_arena *arena __attribute__((unused)),
			void *ptr, size_t size)
{
	return mm_shared_realloc(ptr, size);
}
static void
mm_shared_arena_free(const struct mm_arena *arena __attribute__((unused)),
		     void *ptr)
{
	mm_shared_free(ptr);
}

static const struct mm_arena_vtable mm_shared_arena_vtable = {
	mm_shared_arena_alloc,
	mm_shared_arena_calloc,
	mm_shared_arena_realloc,
	mm_shared_arena_free
};

const struct mm_arena mm_shared_arena = { .vtable = &mm_shared_arena_vtable };
