/*
 * alloc.c - MainMemory memory allocation.
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

#include "alloc.h"

#include "core.h"
#include "lock.h"
#include "log.h"
#include "util.h"

#include "dlmalloc/malloc.h"

const struct mm_allocator mm_alloc_core = {
	mm_core_alloc,
	mm_core_calloc,
	mm_core_realloc,
	mm_core_free
};

const struct mm_allocator mm_alloc_shared = {
	mm_shared_alloc,
	mm_shared_calloc,
	mm_shared_realloc,
	mm_shared_free
};

const struct mm_allocator mm_alloc_global = {
	mm_global_alloc,
	mm_global_calloc,
	mm_global_realloc,
	mm_global_free
};

/**********************************************************************
 * Stubs for LIBC Memory Allocation Routines.
 **********************************************************************/

void *
malloc(size_t size)
{
	mm_libc_call("malloc");
	return mm_global_alloc(size);
}

void *
calloc(size_t count, size_t size)
{
	mm_libc_call("calloc");
	return mm_global_calloc(count, size);
}

void *
realloc(void *ptr, size_t size)
{
	mm_libc_call("realloc");
	return mm_global_realloc(ptr, size);
}

void
free(void *ptr)
{
	// This is called too often by sprintf, suppress it for now.
	//mm_libc_call("free");
	mm_global_free(ptr);
}

/**********************************************************************
 * Intra-core memory allocation routines.
 **********************************************************************/

void *
mm_core_alloc(size_t size)
{
	void *ptr = mspace_malloc(mm_core->arena, size);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_core_alloc_aligned(size_t align, size_t size)
{
	void *ptr = mspace_memalign(mm_core->arena, align, size);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_core_calloc(size_t count, size_t size)
{
	void *ptr = mspace_calloc(mm_core->arena, count, size);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	return ptr;
}

void *
mm_core_realloc(void *ptr, size_t size)
{
	ptr = mspace_realloc(mm_core->arena, ptr, size);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_core_free(void *ptr)
{
	mspace_free(mm_core->arena, ptr);
}

/**********************************************************************
 * Cross-core memory allocation routines.
 **********************************************************************/

static mspace mm_shared_space;

static mm_task_lock_t mm_shared_alloc_lock = MM_TASK_LOCK_INIT;

void
mm_shared_init(void)
{
	ENTER();

	mm_shared_space = create_mspace(0, 0);

	LEAVE();
}

void
mm_shared_term(void)
{
	ENTER();

	destroy_mspace(mm_shared_space);

	LEAVE();
}

void *
mm_shared_alloc(size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	void *ptr = mspace_malloc(mm_shared_space, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_shared_alloc_aligned(size_t align, size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	void *ptr = mspace_memalign(mm_shared_space, align, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_shared_calloc(size_t count, size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	void *ptr = mspace_calloc(mm_shared_space, count, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	return ptr;
}

void *
mm_shared_realloc(void *ptr, size_t size)
{
	mm_task_lock(&mm_shared_alloc_lock);
	ptr = mspace_realloc(mm_shared_space, ptr, size);
	mm_task_unlock(&mm_shared_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_shared_free(void *ptr)
{
	mm_task_lock(&mm_shared_alloc_lock);
	mspace_free(mm_shared_space, ptr);
	mm_task_unlock(&mm_shared_alloc_lock);
}

/**********************************************************************
 * Global memory allocation routines.
 **********************************************************************/

static mm_thread_lock_t mm_global_alloc_lock = MM_THREAD_LOCK_INIT;

void *
mm_global_alloc(size_t size)
{
	mm_thread_lock(&mm_global_alloc_lock);
	void *ptr = dlmalloc(size);
	mm_thread_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_global_alloc_aligned(size_t align, size_t size)
{
	mm_thread_lock(&mm_global_alloc_lock);
	void *ptr = dlmemalign(align, size);
	mm_thread_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_global_calloc(size_t count, size_t size)
{
	mm_thread_lock(&mm_global_alloc_lock);
	void *ptr = dlcalloc(count, size);
	mm_thread_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	return ptr;
}

void *
mm_global_realloc(void *ptr, size_t size)
{
	mm_thread_lock(&mm_global_alloc_lock);
	ptr = dlrealloc(ptr, size);
	mm_thread_unlock(&mm_global_alloc_lock);

	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_global_free(void *ptr)
{
	mm_thread_lock(&mm_global_alloc_lock);
	dlfree(ptr);
	mm_thread_unlock(&mm_global_alloc_lock);
}

/**********************************************************************
 * Memory subsystem initialization and termination.
 **********************************************************************/

void
mm_alloc_init(void)
{
	ENTER();

	dlmallopt(M_GRANULARITY, 16 * MM_PAGE_SIZE);
	mm_shared_init();

	LEAVE();
}

void
mm_alloc_term(void)
{
	ENTER();

	mm_shared_term();

	LEAVE();
}
