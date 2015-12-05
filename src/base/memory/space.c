/*
 * base/memory/space.c - MainMemory memory spaces.
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

#include "base/memory/space.h"

#include "base/ring.h"
#include "base/memory/malloc.h"

/**********************************************************************
 * Low-level memory space routines.
 **********************************************************************/

static mm_mspace_t
mm_mspace_create(void)
{
	mm_mspace_t space;
	space.opaque = create_mspace(0, 0);
	if (space.opaque == NULL)
		mm_fatal(errno, "failed to create mspace");
	return space;
}

size_t
mm_mspace_getallocsize(const void *ptr)
{
	return mspace_usable_size(ptr);
}

size_t
mm_mspace_getfootprint(mm_mspace_t space)
{
	return mspace_footprint(space.opaque);
}

size_t
mm_mspace_getfootprint_limit(mm_mspace_t space)
{
	return mspace_footprint_limit(space.opaque);
}

size_t
mm_mspace_setfootprint_limit(mm_mspace_t space, size_t size)
{
	return mspace_set_footprint_limit(space.opaque, size);
}

/**********************************************************************
 * Basic private memory space routines.
 **********************************************************************/

void * NONNULL(1) MALLOC
mm_private_space_alloc(struct mm_private_space *space, size_t size)
{
	return mspace_malloc(space->space.opaque, size);
}

void * NONNULL(1) MALLOC
mm_private_space_xalloc(struct mm_private_space *space, size_t size)
{
	void *ptr = mm_private_space_alloc(space, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * NONNULL(1) MALLOC
mm_private_space_aligned_alloc(struct mm_private_space *space, size_t align, size_t size)
{
	return mspace_memalign(space->space.opaque, align, size);
}

void * NONNULL(1) MALLOC
mm_private_space_aligned_xalloc(struct mm_private_space *space, size_t align, size_t size)
{
	void *ptr = mm_private_space_aligned_alloc(space, align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * NONNULL(1) MALLOC
mm_private_space_calloc(struct mm_private_space *space, size_t count, size_t size)
{
	return mspace_calloc(space->space.opaque, count, size);
}

void * NONNULL(1) MALLOC
mm_private_space_xcalloc(struct mm_private_space *space, size_t count, size_t size)
{
	void *ptr = mm_private_space_calloc(space, count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * NONNULL(1)
mm_private_space_realloc(struct mm_private_space *space, void *ptr, size_t size)
{
	return mspace_realloc(space->space.opaque, ptr, size);
}

void * NONNULL(1)
mm_private_space_xrealloc(struct mm_private_space *space, void *ptr, size_t size)
{
	ptr = mm_private_space_realloc(space, ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void NONNULL(1)
mm_private_space_free(struct mm_private_space *space, void *ptr)
{
	mspace_free(space->space.opaque, ptr);
}

void NONNULL(1, 2)
mm_private_space_bulk_free(struct mm_private_space *space, void **ptrs, size_t nptrs)
{
	mspace_bulk_free(space->space.opaque, ptrs, nptrs);
}

void NONNULL(1)
mm_private_space_trim(struct mm_private_space *space)
{
	mspace_trim(space->space.opaque, 16 * MM_PAGE_SIZE);
}

bool NONNULL(1, 2)
mm_private_space_enqueue(struct mm_private_space *space, void *ptr)
{
	return mm_ring_spsc_locked_put(space->reclaim_queue, ptr);
}

bool NONNULL(1)
mm_private_space_reclaim(struct mm_private_space *space)
{
	bool rc = false;

	void *ptr;
	while (mm_ring_spsc_get(space->reclaim_queue, &ptr)) {
		mm_private_space_free(space, ptr);
		rc = true;
	}

	return rc;
}

/**********************************************************************
 * Private memory space arenas.
 **********************************************************************/

#define PRIVATE_UARENA_SPACE(x)	containerof(x, struct mm_private_space, uarena)
#define PRIVATE_XARENA_SPACE(x)	containerof(x, struct mm_private_space, xarena)

static void *
mm_private_uarena_alloc(mm_arena_t arena, size_t size)
{
	struct mm_private_space *space = PRIVATE_UARENA_SPACE(arena);
	return mm_private_space_alloc(space, size);
}

static void *
mm_private_uarena_calloc(mm_arena_t arena, size_t count, size_t size)
{
	struct mm_private_space *space = PRIVATE_UARENA_SPACE(arena);
	return mm_private_space_calloc(space, count, size);
}

static void *
mm_private_uarena_realloc(mm_arena_t arena, void *ptr, size_t size)
{
	struct mm_private_space *space = PRIVATE_UARENA_SPACE(arena);
	return mm_private_space_realloc(space, ptr, size);
}

static void
mm_private_uarena_free(mm_arena_t arena, void *ptr)
{
	struct mm_private_space *space = PRIVATE_UARENA_SPACE(arena);
	mm_private_space_free(space, ptr);
}

static void *
mm_private_xarena_alloc(mm_arena_t arena, size_t size)
{
	struct mm_private_space *space = PRIVATE_XARENA_SPACE(arena);
	return mm_private_space_xalloc(space, size);
}

static void *
mm_private_xarena_calloc(mm_arena_t arena, size_t count, size_t size)
{
	struct mm_private_space *space = PRIVATE_XARENA_SPACE(arena);
	return mm_private_space_xcalloc(space, count, size);
}

static void *
mm_private_xarena_realloc(mm_arena_t arena, void *ptr, size_t size)
{
	struct mm_private_space *space = PRIVATE_XARENA_SPACE(arena);
	return mm_private_space_xrealloc(space, ptr, size);
}

static void
mm_private_xarena_free(mm_arena_t arena, void *ptr)
{
	struct mm_private_space *space = PRIVATE_XARENA_SPACE(arena);
	mm_private_space_free(space, ptr);
}

MM_ARENA_VTABLE(mm_private_uarena_vtable,
	mm_private_uarena_alloc,
	mm_private_uarena_calloc,
	mm_private_uarena_realloc,
	mm_private_uarena_free);

MM_ARENA_VTABLE(mm_private_xarena_vtable,
	mm_private_xarena_alloc,
	mm_private_xarena_calloc,
	mm_private_xarena_realloc,
	mm_private_xarena_free);

/**********************************************************************
 * Private memory space initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_private_space_prepare(struct mm_private_space *space, uint32_t queue_size)
{
	space->space = mm_mspace_create();
	space->uarena.vtable = &mm_private_uarena_vtable;
	space->xarena.vtable = &mm_private_xarena_vtable;

	if (queue_size == 0)
		space->reclaim_queue = NULL;
	else
		space->reclaim_queue = mm_ring_spsc_create(queue_size,
							   MM_RING_LOCKED_PUT);
}

void NONNULL(1)
mm_private_space_cleanup(struct mm_private_space *space)
{
	if (space->reclaim_queue != NULL)
		mm_ring_spsc_destroy(space->reclaim_queue);
	destroy_mspace(space->space.opaque);
}

/**********************************************************************
 * Basic shared memory space routines.
 **********************************************************************/

void * NONNULL(1) MALLOC
mm_shared_space_alloc(struct mm_shared_space *space, size_t size)
{
	mm_common_lock(&space->lock);
	void *ptr = mspace_malloc(space->space.opaque, size);
	mm_common_unlock(&space->lock);
	return ptr;
}

void * NONNULL(1) MALLOC
mm_shared_space_xalloc(struct mm_shared_space *space, size_t size)
{
	void *ptr = mm_shared_space_alloc(space, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * NONNULL(1) MALLOC
mm_shared_space_aligned_alloc(struct mm_shared_space *space, size_t align, size_t size)
{
	mm_common_lock(&space->lock);
	void *ptr = mspace_memalign(space->space.opaque, align, size);
	mm_common_unlock(&space->lock);
	return ptr;
}

void * NONNULL(1) MALLOC
mm_shared_space_aligned_xalloc(struct mm_shared_space *space, size_t align, size_t size)
{
	void *ptr = mm_shared_space_aligned_alloc(space, align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * NONNULL(1) MALLOC
mm_shared_space_calloc(struct mm_shared_space *space, size_t count, size_t size)
{
	mm_common_lock(&space->lock);
	void *ptr = mspace_calloc(space->space.opaque, count, size);
	mm_common_unlock(&space->lock);
	return ptr;
}

void * NONNULL(1) MALLOC
mm_shared_space_xcalloc(struct mm_shared_space *space, size_t count, size_t size)
{
	void *ptr = mm_shared_space_calloc(space, count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void * NONNULL(1)
mm_shared_space_realloc(struct mm_shared_space *space, void *ptr, size_t size)
{
	mm_common_lock(&space->lock);
	ptr = mspace_realloc(space->space.opaque, ptr, size);
	mm_common_unlock(&space->lock);
	return ptr;
}

void * NONNULL(1)
mm_shared_space_xrealloc(struct mm_shared_space *space, void *ptr, size_t size)
{
	ptr = mm_shared_space_realloc(space, ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void NONNULL(1)
mm_shared_space_free(struct mm_shared_space *space, void *ptr)
{
	mm_common_lock(&space->lock);
	mspace_free(space->space.opaque, ptr);
	mm_common_unlock(&space->lock);
}

void NONNULL(1, 2)
mm_shared_space_bulk_free(struct mm_shared_space *space, void **ptrs, size_t nptrs)
{
	mm_common_lock(&space->lock);
	mspace_bulk_free(space->space.opaque, ptrs, nptrs);
	mm_common_unlock(&space->lock);
}

void NONNULL(1)
mm_shared_space_trim(struct mm_shared_space *space)
{
	mm_common_lock(&space->lock);
	mspace_trim(space->space.opaque, 64 * MM_PAGE_SIZE);
	mm_common_unlock(&space->lock);
}

/**********************************************************************
 * Shared memory space arenas.
 **********************************************************************/

#define SHARED_UARENA_SPACE(x)	containerof(x, struct mm_shared_space, uarena)
#define SHARED_XARENA_SPACE(x)	containerof(x, struct mm_shared_space, xarena)

static void *
mm_shared_uarena_alloc(mm_arena_t arena, size_t size)
{
	struct mm_shared_space *space = SHARED_UARENA_SPACE(arena);
	return mm_shared_space_alloc(space, size);
}

static void *
mm_shared_uarena_calloc(mm_arena_t arena, size_t count, size_t size)
{
	struct mm_shared_space *space = SHARED_UARENA_SPACE(arena);
	return mm_shared_space_calloc(space, count, size);
}

static void *
mm_shared_uarena_realloc(mm_arena_t arena, void *ptr, size_t size)
{
	struct mm_shared_space *space = SHARED_UARENA_SPACE(arena);
	return mm_shared_space_realloc(space, ptr, size);
}

static void
mm_shared_uarena_free(mm_arena_t arena, void *ptr)
{
	struct mm_shared_space *space = SHARED_UARENA_SPACE(arena);
	mm_shared_space_free(space, ptr);
}

static void *
mm_shared_xarena_alloc(mm_arena_t arena, size_t size)
{
	struct mm_shared_space *space = SHARED_XARENA_SPACE(arena);
	return mm_shared_space_xalloc(space, size);
}

static void *
mm_shared_xarena_calloc(mm_arena_t arena, size_t count, size_t size)
{
	struct mm_shared_space *space = SHARED_XARENA_SPACE(arena);
	return mm_shared_space_xcalloc(space, count, size);
}

static void *
mm_shared_xarena_realloc(mm_arena_t arena, void *ptr, size_t size)
{
	struct mm_shared_space *space = SHARED_XARENA_SPACE(arena);
	return mm_shared_space_xrealloc(space, ptr, size);
}

static void
mm_shared_xarena_free(mm_arena_t arena, void *ptr)
{
	struct mm_shared_space *space = SHARED_XARENA_SPACE(arena);
	mm_shared_space_free(space, ptr);
}

MM_ARENA_VTABLE(mm_shared_uarena_vtable,
	mm_shared_uarena_alloc,
	mm_shared_uarena_calloc,
	mm_shared_uarena_realloc,
	mm_shared_uarena_free);

MM_ARENA_VTABLE(mm_shared_xarena_vtable,
	mm_shared_xarena_alloc,
	mm_shared_xarena_calloc,
	mm_shared_xarena_realloc,
	mm_shared_xarena_free);

/**********************************************************************
 * Shared memory space initialization and termination.
 **********************************************************************/

void NONNULL(1)
mm_shared_space_prepare(struct mm_shared_space *space)
{
	space->space = mm_mspace_create();
	space->uarena.vtable = &mm_shared_uarena_vtable;
	space->xarena.vtable = &mm_shared_xarena_vtable;
	space->lock = (mm_common_lock_t) MM_COMMON_LOCK_INIT;
}

void NONNULL(1)
mm_shared_space_cleanup(struct mm_shared_space *space)
{
	destroy_mspace(space->space.opaque);
}

/**********************************************************************
 * Memory space subsystem initialization.
 **********************************************************************/

void
mm_space_init()
{
	dlmallopt(M_GRANULARITY, 16 * MM_PAGE_SIZE);
}
