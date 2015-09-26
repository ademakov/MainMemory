/*
 * base/memory/space.c - MainMemory memory spaces.
 *
 * Copyright (C) 2014-2015  Aleksey Demakov
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

/**********************************************************************
 * Private Memory Space.
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

void __attribute__((nonnull(1)))
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

void __attribute__((nonnull(1)))
mm_private_space_cleanup(struct mm_private_space *space)
{
	if (space->reclaim_queue != NULL)
		mm_ring_spsc_destroy(space->reclaim_queue);
	mm_mspace_destroy(space->space);
}

bool __attribute__((nonnull(1, 2)))
mm_private_space_enqueue(struct mm_private_space *space, void *ptr)
{
	return mm_ring_spsc_locked_put(space->reclaim_queue, ptr);
}

bool __attribute__((nonnull(1)))
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
 * Shared Memory Space.
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

void __attribute__((nonnull(1)))
mm_shared_space_prepare(struct mm_shared_space *space)
{
	space->space = mm_mspace_create();
	space->uarena.vtable = &mm_shared_uarena_vtable;
	space->xarena.vtable = &mm_shared_xarena_vtable;
	space->lock = (mm_common_lock_t) MM_COMMON_LOCK_INIT;
}

void __attribute__((nonnull(1)))
mm_shared_space_cleanup(struct mm_shared_space *space)
{
	mm_mspace_destroy(space->space);
}
