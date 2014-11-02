/*
 * base/mem/space.c - MainMemory memory spaces.
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

#include "base/mem/space.h"
#include "base/log/error.h"

/**********************************************************************
 * Private Memory Space.
 **********************************************************************/

MM_ARENA_VTABLE(mm_private_arena_vtable,
	mm_private_space_alloc,
	mm_private_space_calloc,
	mm_private_space_realloc,
	mm_private_space_free);

MM_ARENA_VTABLE(mm_private_xarena_vtable,
	mm_private_space_xalloc,
	mm_private_space_xcalloc,
	mm_private_space_xrealloc,
	mm_private_space_free);

void
mm_private_space_prepare(struct mm_private_space *space, bool xarena)
{
	if (xarena)
		space->arena.vtable = &mm_private_xarena_vtable;
	else
		space->arena.vtable = &mm_private_arena_vtable;
	space->space = mm_mspace_create();
}

void
mm_private_space_cleanup(struct mm_private_space *space)
{
	mm_mspace_destroy(space->space);
}

void *
mm_private_space_alloc(struct mm_private_space *space, size_t size)
{
	return mm_mspace_alloc(space->space, size);
}

void *
mm_private_space_xalloc(struct mm_private_space *space, size_t size)
{
	void *ptr = mm_mspace_alloc(space->space, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_private_space_aligned_alloc(struct mm_private_space *space, size_t align, size_t size)
{
	return mm_mspace_aligned_alloc(space->space, align, size);
}

void *
mm_private_space_aligned_xalloc(struct mm_private_space *space, size_t align, size_t size)
{
	void *ptr = mm_mspace_aligned_alloc(space->space, align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_private_space_calloc(struct mm_private_space *space, size_t count, size_t size)
{
	return mm_mspace_calloc(space->space, count, size);
}

void *
mm_private_space_xcalloc(struct mm_private_space *space, size_t count, size_t size)
{
	void *ptr = mm_mspace_calloc(space->space, count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_private_space_realloc(struct mm_private_space *space, void *ptr, size_t size)
{
	return mm_mspace_realloc(space->space, ptr, size);
}

void *
mm_private_space_xrealloc(struct mm_private_space *space, void *ptr, size_t size)
{
	ptr = mm_mspace_realloc(space->space, ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_private_space_free(struct mm_private_space *space, void *ptr)
{
	mm_mspace_free(space->space, ptr);
}

/**********************************************************************
 * Common Memory Space.
 **********************************************************************/

MM_ARENA_VTABLE(mm_common_arena_vtable,
	mm_common_space_alloc,
	mm_common_space_calloc,
	mm_common_space_realloc,
	mm_common_space_free);

MM_ARENA_VTABLE(mm_common_xarena_vtable,
	mm_common_space_xalloc,
	mm_common_space_xcalloc,
	mm_common_space_xrealloc,
	mm_common_space_free);

void
mm_common_space_prepare(struct mm_common_space *space, bool xarena)
{
	if (xarena)
		space->arena.vtable = &mm_common_xarena_vtable;
	else
		space->arena.vtable = &mm_common_arena_vtable;
	space->space = mm_mspace_create();
	space->lock = (mm_thread_lock_t) MM_THREAD_LOCK_INIT;
}

void
mm_common_space_cleanup(struct mm_common_space *space)
{
	mm_mspace_destroy(space->space);
}

void *
mm_common_space_alloc(struct mm_common_space *space, size_t size)
{
	mm_thread_lock(&space->lock);
	void *ptr = mm_mspace_alloc(space->space, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

void *
mm_common_space_xalloc(struct mm_common_space *space, size_t size)
{
	void *ptr = mm_common_space_alloc(space, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_common_space_aligned_alloc(struct mm_common_space *space, size_t align, size_t size)
{
	mm_thread_lock(&space->lock);
	void *ptr = mm_mspace_aligned_alloc(space->space, align, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

void *
mm_common_space_aligned_xalloc(struct mm_common_space *space, size_t align, size_t size)
{
	void *ptr = mm_common_space_aligned_alloc(space, align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_common_space_calloc(struct mm_common_space *space, size_t count, size_t size)
{
	mm_thread_lock(&space->lock);
	void *ptr = mm_mspace_calloc(space->space, count, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

void *
mm_common_space_xcalloc(struct mm_common_space *space, size_t count, size_t size)
{
	void *ptr = mm_common_space_calloc(space, count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void *
mm_common_space_realloc(struct mm_common_space *space, void *ptr, size_t size)
{
	mm_thread_lock(&space->lock);
	ptr = mm_mspace_realloc(space->space, ptr, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

void *
mm_common_space_xrealloc(struct mm_common_space *space, void *ptr, size_t size)
{
	ptr = mm_common_space_realloc(space, ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

void
mm_common_space_free(struct mm_common_space *space, void *ptr)
{
	mm_thread_lock(&space->lock);
	mm_mspace_free(space->space, ptr);
	mm_thread_unlock(&space->lock);
}

/**********************************************************************
 * Common Memory Space Instance.
 **********************************************************************/

struct mm_common_space mm_common_space;

void
mm_common_space_init(void)
{
	mm_common_space_prepare(&mm_common_space, true);
}

void
mm_common_space_term(void)
{
	mm_common_space_cleanup(&mm_common_space);
}
