/*
 * base/mem/space.h - MainMemory memory spaces.
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

#ifndef BASE_MEM_SPACE_H
#define	BASE_MEM_SPACE_H

#include "common.h"
#include "base/lock.h"
#include "base/log/error.h"
#include "base/mem/alloc.h"
#include "base/mem/arena.h"

/**********************************************************************
 * Private Memory Space.
 **********************************************************************/

struct mm_private_space
{
	/* The underlying memory space. */
	mm_mspace_t space;
	/* Memory arena without error checking (using *_alloc family). */
	struct mm_arena uarena;
	/* Memory arena with error checking (using *_xalloc family). */
	struct mm_arena xarena;
};

void __attribute__((nonnull(1)))
mm_private_space_prepare(struct mm_private_space *space);

void __attribute__((nonnull(1)))
mm_private_space_cleanup(struct mm_private_space *space);

static inline void *
mm_private_space_alloc(struct mm_private_space *space, size_t size)
{
	return mm_mspace_alloc(space->space, size);
}

static inline void *
mm_private_space_xalloc(struct mm_private_space *space, size_t size)
{
	void *ptr = mm_mspace_alloc(space->space, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void *
mm_private_space_aligned_alloc(struct mm_private_space *space, size_t align, size_t size)
{
	return mm_mspace_aligned_alloc(space->space, align, size);
}

static inline void *
mm_private_space_aligned_xalloc(struct mm_private_space *space, size_t align, size_t size)
{
	void *ptr = mm_mspace_aligned_alloc(space->space, align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void *
mm_private_space_calloc(struct mm_private_space *space, size_t count, size_t size)
{
	return mm_mspace_calloc(space->space, count, size);
}

static inline void *
mm_private_space_xcalloc(struct mm_private_space *space, size_t count, size_t size)
{
	void *ptr = mm_mspace_calloc(space->space, count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void *
mm_private_space_realloc(struct mm_private_space *space, void *ptr, size_t size)
{
	return mm_mspace_realloc(space->space, ptr, size);
}

static inline void *
mm_private_space_xrealloc(struct mm_private_space *space, void *ptr, size_t size)
{
	ptr = mm_mspace_realloc(space->space, ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void
mm_private_space_free(struct mm_private_space *space, void *ptr)
{
	mm_mspace_free(space->space, ptr);
}

static inline void
mm_private_space_bulk_free(struct mm_private_space *space, void **ptrs, size_t nptrs)
{
	mm_mspace_bulk_free(space->space, ptrs, nptrs);
}

/**********************************************************************
 * Shared Memory Space.
 **********************************************************************/

struct mm_shared_space
{
	/* The underlying memory space. */
	mm_mspace_t space;
	/* Memory arena without error checking (using *_alloc family). */
	struct mm_arena uarena;
	/* Memory arena with error checking (using *_xalloc family). */
	struct mm_arena xarena;
	/* Concurrent access lock. */
	mm_thread_lock_t lock;
};

void __attribute__((nonnull(1)))
mm_shared_space_prepare(struct mm_shared_space *space);

void __attribute__((nonnull(1)))
mm_shared_space_cleanup(struct mm_shared_space *space);

static inline void *
mm_shared_space_alloc(struct mm_shared_space *space, size_t size)
{
	mm_thread_lock(&space->lock);
	void *ptr = mm_mspace_alloc(space->space, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

static inline void *
mm_shared_space_xalloc(struct mm_shared_space *space, size_t size)
{
	void *ptr = mm_shared_space_alloc(space, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void *
mm_shared_space_aligned_alloc(struct mm_shared_space *space, size_t align, size_t size)
{
	mm_thread_lock(&space->lock);
	void *ptr = mm_mspace_aligned_alloc(space->space, align, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

static inline void *
mm_shared_space_aligned_xalloc(struct mm_shared_space *space, size_t align, size_t size)
{
	void *ptr = mm_shared_space_aligned_alloc(space, align, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void *
mm_shared_space_calloc(struct mm_shared_space *space, size_t count, size_t size)
{
	mm_thread_lock(&space->lock);
	void *ptr = mm_mspace_calloc(space->space, count, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

static inline void *
mm_shared_space_xcalloc(struct mm_shared_space *space, size_t count, size_t size)
{
	void *ptr = mm_shared_space_calloc(space, count, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void *
mm_shared_space_realloc(struct mm_shared_space *space, void *ptr, size_t size)
{
	mm_thread_lock(&space->lock);
	ptr = mm_mspace_realloc(space->space, ptr, size);
	mm_thread_unlock(&space->lock);
	return ptr;
}

static inline void *
mm_shared_space_xrealloc(struct mm_shared_space *space, void *ptr, size_t size)
{
	ptr = mm_shared_space_realloc(space, ptr, size);
	if (unlikely(ptr == NULL))
		mm_fatal(errno, "error allocating %zu bytes of memory", size);
	return ptr;
}

static inline void
mm_shared_space_free(struct mm_shared_space *space, void *ptr)
{
	mm_thread_lock(&space->lock);
	mm_mspace_free(space->space, ptr);
	mm_thread_unlock(&space->lock);
}

static inline void __attribute__((nonnull(1)))
mm_shared_space_bulk_free(struct mm_shared_space *space, void **ptrs, size_t nptrs)
{
	mm_thread_lock(&space->lock);
	mm_mspace_bulk_free(space->space, ptrs, nptrs);
	mm_thread_lock(&space->lock);
}

#endif /* BASE_MEM_SPACE_H */
