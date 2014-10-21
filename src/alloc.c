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
#include "trace.h"
#include "util.h"

#include "dlmalloc/malloc.h"

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
mm_local_alloc(size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_mspace_xalloc(core->space, size);
}

void *
mm_local_aligned_alloc(size_t align, size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_mspace_aligned_xalloc(core->space, align, size);
}

void *
mm_local_calloc(size_t count, size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_mspace_xcalloc(core->space, count, size);
}

void *
mm_local_realloc(void *ptr, size_t size)
{
	struct mm_core *core = mm_core_self();
	return mm_mspace_xrealloc(core->space, ptr, size);
}

void
mm_local_free(void *ptr)
{
	struct mm_core *core = mm_core_self();
	mm_mspace_free(core->space, ptr);
}

size_t
mm_local_alloc_size(const void *ptr)
{
	return mm_mspace_getallocsize(ptr);
}

void *
mm_local_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_local_alloc(size), ptr, size);
}

char *
mm_local_strdup(const char *ptr)
{
	return mm_local_memdup(ptr, strlen(ptr) + 1);
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
mm_shared_aligned_alloc(size_t align, size_t size)
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

void *
mm_shared_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_shared_alloc(size), ptr, size);
}

char *
mm_shared_strdup(const char *ptr)
{
	return mm_shared_memdup(ptr, strlen(ptr) + 1);
}

void
mm_shared_free(void *ptr)
{
	mm_task_lock(&mm_shared_alloc_lock);
	mspace_free(mm_shared_space, ptr);
	mm_task_unlock(&mm_shared_alloc_lock);
}

size_t
mm_shared_alloc_size(const void *ptr)
{
	return mspace_usable_size(ptr);
}

/**********************************************************************
 * Global memory allocation routines.
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

void *
mm_global_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_global_alloc(size), ptr, size);
}

char *
mm_global_strdup(const char *ptr)
{
	return mm_global_memdup(ptr, strlen(ptr) + 1);
}

void
mm_global_free(void *ptr)
{
	mm_global_lock(&mm_global_alloc_lock);
	dlfree(ptr);
	mm_global_unlock(&mm_global_alloc_lock);
}

size_t
mm_global_alloc_size(const void *ptr)
{
	return dlmalloc_usable_size(ptr);
}

/**********************************************************************
 * Memory Space Allocation Routines.
 **********************************************************************/

mm_mspace_t
mm_mspace_create(void)
{
	mm_mspace_t space = create_mspace(0, 0);
	if (space == NULL)
		mm_fatal(errno, "failed to create mspace");
	return space;
}

void
mm_mspace_destroy(mm_mspace_t space)
{
	destroy_mspace(space);
}

void *
mm_mspace_alloc(mm_mspace_t space, size_t size)
{
	return mspace_malloc(space, size);
}

void *
mm_mspace_xalloc(mm_mspace_t space, size_t size)
{
	void *ptr = mm_mspace_alloc(space, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_mspace_aligned_alloc(mm_mspace_t space, size_t align, size_t size)
{
	return mspace_memalign(space, align, size);
}

void *
mm_mspace_aligned_xalloc(mm_mspace_t space, size_t align, size_t size)
{
	void *ptr = mm_mspace_aligned_alloc(space, align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_mspace_calloc(mm_mspace_t space, size_t count, size_t size)
{
	return mspace_calloc(space, count, size);
}

void *
mm_mspace_xcalloc(mm_mspace_t space, size_t count, size_t size)
{
	void *ptr = mm_mspace_calloc(space, count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", count * size);
	return ptr;
}

void *
mm_mspace_realloc(mm_mspace_t space, void *ptr, size_t size)
{
	return mspace_realloc(space, ptr, size);
}

void *
mm_mspace_xrealloc(mm_mspace_t space, void *ptr, size_t size)
{
	ptr = mm_mspace_realloc(space, ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_mspace_free(mm_mspace_t space, void *ptr)
{
	mspace_free(space, ptr);
}

size_t
mm_mspace_getfootprint(mm_mspace_t space)
{
	return mspace_footprint(space);
}

size_t
mm_mspace_getfootprint_limit(mm_mspace_t space)
{
	return mspace_footprint_limit(space);
}

size_t
mm_mspace_setfootprint_limit(mm_mspace_t space, size_t size)
{
	return mspace_set_footprint_limit(space, size);
}

size_t
mm_mspace_getallocsize(const void *ptr)
{
	return mspace_usable_size(ptr);
}

/**********************************************************************
 * Simple Memory Arenas.
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

static void *
mm_global_arena_alloc(const struct mm_arena *arena __attribute__((unused)),
		      size_t size)
{
	return mm_global_alloc(size);
}
static void *
mm_global_arena_calloc(const struct mm_arena *arena __attribute__((unused)),
		       size_t count, size_t size)
{
	return mm_global_calloc(count, size);
}
static void *
mm_global_arena_realloc(const struct mm_arena *arena __attribute__((unused)),
			void *ptr, size_t size)
{
	return mm_global_realloc(ptr, size);
}
static void
mm_global_arena_free(const struct mm_arena *arena __attribute__((unused)),
		     void *ptr)
{
	mm_global_free(ptr);
}

static const struct mm_arena_vtable mm_shared_arena_vtable = {
	mm_shared_arena_alloc,
	mm_shared_arena_calloc,
	mm_shared_arena_realloc,
	mm_shared_arena_free
};

static const struct mm_arena_vtable mm_global_arena_vtable = {
	mm_global_arena_alloc,
	mm_global_arena_calloc,
	mm_global_arena_realloc,
	mm_global_arena_free
};

const struct mm_arena mm_shared_arena = { .vtable = &mm_shared_arena_vtable };
const struct mm_arena mm_global_arena = { .vtable = &mm_global_arena_vtable };

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
