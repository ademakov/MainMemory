/*
 * base/mem/space.h - MainMemory memory spaces.
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

#ifndef BASE_MEM_SPACE_H
#define	BASE_MEM_SPACE_H

#include "common.h"
#include "base/lock.h"
#include "base/mem/alloc.h"
#include "base/mem/arena.h"

/**********************************************************************
 * Private Memory Space.
 **********************************************************************/

struct mm_private_space
{
	/* Arena must be the first field in the structure. */
	struct mm_arena arena;
	/* The underlying memory space. */
	mm_mspace_t space;
};

void mm_private_space_prepare(struct mm_private_space *space, bool xarena)
	__attribute__((nonnull(1)));

void mm_private_space_cleanup(struct mm_private_space *space)
	__attribute__((nonnull(1)));

void * mm_private_space_alloc(struct mm_private_space *space, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_private_space_xalloc(struct mm_private_space *space, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_private_space_aligned_alloc(struct mm_private_space *space, size_t align, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_private_space_aligned_xalloc(struct mm_private_space *space, size_t align, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_private_space_calloc(struct mm_private_space *space, size_t count, size_t size)
	__attribute__((malloc));

void * mm_private_space_xcalloc(struct mm_private_space *space, size_t count, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_private_space_realloc(struct mm_private_space *space, void *ptr, size_t size)
	__attribute__((nonnull(1)));

void * mm_private_space_xrealloc(struct mm_private_space *space, void *ptr, size_t size)
	__attribute__((nonnull(1)));

void mm_private_space_free(struct mm_private_space *space, void *ptr)
	__attribute__((nonnull(1)));

static inline void __attribute__((nonnull(1)))
mm_private_space_bulk_free(struct mm_private_space *space, void **ptrs, size_t nptrs)
{
	mm_mspace_bulk_free(space->space, ptrs, nptrs);
}

/**********************************************************************
 * Common Memory Space.
 **********************************************************************/

struct mm_common_space
{
	/* Arena must be the first field in the structure. */
	struct mm_arena arena;
	/* The underlying memory space. */
	mm_mspace_t space;
	/* Concurrent access lock. */
	mm_thread_lock_t lock;
};

void mm_common_space_prepare(struct mm_common_space *space, bool xarena)
	__attribute__((nonnull(1)));

void mm_common_space_cleanup(struct mm_common_space *space)
	__attribute__((nonnull(1)));

void * mm_common_space_alloc(struct mm_common_space *space, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_common_space_xalloc(struct mm_common_space *space, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_common_space_aligned_alloc(struct mm_common_space *space, size_t align, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_common_space_aligned_xalloc(struct mm_common_space *space, size_t align, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_common_space_calloc(struct mm_common_space *space, size_t count, size_t size)
	__attribute__((malloc));

void * mm_common_space_xcalloc(struct mm_common_space *space, size_t count, size_t size)
	__attribute__((nonnull(1)))
	__attribute__((malloc));

void * mm_common_space_realloc(struct mm_common_space *space, void *ptr, size_t size)
	__attribute__((nonnull(1)));

void * mm_common_space_xrealloc(struct mm_common_space *space, void *ptr, size_t size)
	__attribute__((nonnull(1)));

void mm_common_space_free(struct mm_common_space *space, void *ptr)
	__attribute__((nonnull(1)));

static inline void __attribute__((nonnull(1)))
mm_common_space_bulk_free(struct mm_common_space *space, void **ptrs, size_t nptrs)
{
	mm_thread_lock(&space->lock);
	mm_mspace_bulk_free(space->space, ptrs, nptrs);
	mm_thread_lock(&space->lock);
}

/**********************************************************************
 * Common Memory Space Default Instance.
 **********************************************************************/

extern struct mm_common_space mm_common_space;

void mm_common_space_init(void);
void mm_common_space_term(void);

static inline bool
mm_common_space_is_ready(void)
{
	return (mm_common_space.arena.vtable != NULL);
}

static inline void *
mm_common_alloc(size_t size)
{
	return mm_common_space_xalloc(&mm_common_space, size);
}

static inline void *
mm_common_aligned_alloc(size_t align, size_t size)
{
	return mm_common_space_aligned_xalloc(&mm_common_space, align, size);
}

static inline void *
mm_common_calloc(size_t count, size_t size)
{
	return mm_common_space_xcalloc(&mm_common_space, count, size);
}

static inline void *
mm_common_realloc( void *ptr, size_t size)
{
	return mm_common_space_xrealloc(&mm_common_space, ptr, size);
}

static inline void
mm_common_free(void *ptr)
{
	mm_common_space_free(&mm_common_space, ptr);
}

/**********************************************************************
 * Common Memory Allocation Utilities.
 **********************************************************************/

static inline void *
mm_common_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_common_alloc(size), ptr, size);
}

static inline char *
mm_common_strdup(const char *ptr)
{
	return mm_common_memdup(ptr, strlen(ptr) + 1);
}

#endif /* BASE_MEM_SPACE_H */
