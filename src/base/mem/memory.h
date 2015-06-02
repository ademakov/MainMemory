/*
 * base/memory/memory.h - MainMemory memory subsystem.
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

#ifndef BASE_MEMORY_MEMORY_H
#define BASE_MEMORY_MEMORY_H

#include "common.h"
#include "base/mem/alloc.h"
#include "base/mem/arena.h"
#include "base/mem/chunk.h"
#include "base/mem/space.h"
#include "base/thread/thread.h"

/**********************************************************************
 * Common Memory Space.
 **********************************************************************/

extern struct mm_shared_space mm_common_space;

static inline bool
mm_common_space_ready(void)
{
	return (mm_common_space.xarena.vtable != NULL);
}

static inline void
mm_common_space_reset(void)
{
	mm_common_space.xarena.vtable = NULL;
}

static inline void *
mm_common_alloc(size_t size)
{
	return mm_shared_space_xalloc(&mm_common_space, size);
}

static inline void *
mm_common_aligned_alloc(size_t align, size_t size)
{
	return mm_shared_space_aligned_xalloc(&mm_common_space, align, size);
}

static inline void *
mm_common_calloc(size_t count, size_t size)
{
	return mm_shared_space_xcalloc(&mm_common_space, count, size);
}

static inline void *
mm_common_realloc( void *ptr, size_t size)
{
	return mm_shared_space_xrealloc(&mm_common_space, ptr, size);
}

static inline void
mm_common_free(void *ptr)
{
	mm_shared_space_free(&mm_common_space, ptr);
}

/**********************************************************************
 * Memory Allocation Routines for Regular Threads.
 **********************************************************************/

/*
 * NB: This code implies there is only one regular thread in non-SMP mode.
 */

#if ENABLE_SMP
extern struct mm_shared_space mm_regular_space;
#else
extern struct mm_private_space mm_regular_space;
#endif

static inline void *
mm_regular_alloc(size_t size)
{
#if ENABLE_SMP
	return mm_shared_space_xalloc(&mm_regular_space, size);
#else
	return mm_private_space_xalloc(&mm_regular_space, size);
#endif
}

static inline void *
mm_regular_aligned_alloc(size_t align, size_t size)
{
#if ENABLE_SMP
	return mm_shared_space_aligned_xalloc(&mm_regular_space, align, size);
#else
	return mm_private_space_aligned_alloc(&mm_regular_space, align, size);
#endif
}

static inline void *
mm_regular_calloc(size_t count, size_t size)
{
#if ENABLE_SMP
	return mm_shared_space_xcalloc(&mm_regular_space, count, size);
#else
	return mm_private_space_calloc(&mm_regular_space, count, size);
#endif
}

static inline void *
mm_regular_realloc(void *ptr, size_t size)
{
#if ENABLE_SMP
	return mm_shared_space_xrealloc(&mm_regular_space, ptr, size);
#else
	return mm_private_space_realloc(&mm_regular_space, ptr, size);
#endif
}

static inline void
mm_regular_free(void *ptr)
{
#if ENABLE_SMP
	mm_shared_space_free(&mm_regular_space, ptr);
#else
	mm_private_space_free(&mm_regular_space, ptr);
#endif
}

/**********************************************************************
 * Private Memory Allocation Routines for Single Thread.
 **********************************************************************/

static inline bool
mm_private_space_ready(struct mm_private_space *space)
{
	return space->space.opaque != NULL;
}

static inline struct mm_private_space *
mm_private_space_get(void)
{
#if ENABLE_SMP
	struct mm_thread *thread = mm_thread_self();
	struct mm_private_space *space = mm_thread_getspace(thread);
	if (unlikely(!mm_private_space_ready(space)))
		return NULL;
	return space;
#else
	return &mm_regular_space;
#endif
}

static inline void *
mm_private_alloc(size_t size)
{
#if ENABLE_SMP
	struct mm_private_space *space = mm_private_space_get();
	if (likely(space != NULL))
		return mm_private_space_xalloc(space, size);
#endif
	return mm_regular_alloc(size);
}

static inline void *
mm_private_aligned_alloc(size_t align, size_t size)
{
#if ENABLE_SMP
	struct mm_private_space *space = mm_private_space_get();
	if (likely(space != NULL))
		return mm_private_space_aligned_xalloc(space, align, size);
#endif
	return mm_regular_aligned_alloc(align, size);
}

static inline void *
mm_private_calloc(size_t count, size_t size)
{
#if ENABLE_SMP
	struct mm_private_space *space = mm_private_space_get();
	if (likely(space != NULL))
		return mm_private_space_xcalloc(space, count, size);
#endif
	return mm_regular_calloc(count, size);
}

static inline void *
mm_private_realloc(void *ptr, size_t size)
{
#if ENABLE_SMP
	struct mm_private_space *space = mm_private_space_get();
	if (likely(space != NULL))
		return mm_private_space_xrealloc(space, ptr, size);
#endif
	return mm_regular_realloc(ptr, size);
}

static inline void
mm_private_free(void *ptr)
{
#if ENABLE_SMP
	struct mm_private_space *space = mm_private_space_get();
	if (likely(space != NULL)) {
		mm_private_space_free(space, ptr);
		return;
	}
#endif
	mm_regular_free(ptr);
}

/**********************************************************************
 * Global Memory Allocation Utilities.
 **********************************************************************/

static inline void *
mm_global_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_global_alloc(size), ptr, size);
}

static inline char *
mm_global_strdup(const char *ptr)
{
	return mm_global_memdup(ptr, strlen(ptr) + 1);
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

/**********************************************************************
 * Memory Allocation Utilities for Regular Threads.
 **********************************************************************/

static inline void *
mm_regular_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_regular_alloc(size), ptr, size);
}

static inline char *
mm_regular_strdup(const char *ptr)
{
	return mm_regular_memdup(ptr, strlen(ptr) + 1);
}

/**********************************************************************
 * Private Memory Allocation Utilities for Single Thread.
 **********************************************************************/

static inline void *
mm_private_memdup(const void *ptr, size_t size)
{
	return memcpy(mm_private_alloc(size), ptr, size);
}

static inline char *
mm_private_strdup(const char *ptr)
{
	return mm_private_memdup(ptr, strlen(ptr) + 1);
}

/**********************************************************************
 * Memory Subsystem Initialization and Termination.
 **********************************************************************/

struct mm_memory_params
{
	mm_chunk_free_t free;
};

void mm_memory_init(struct mm_memory_params *params);

void mm_memory_term(void);

#endif /* BASE_MEMORY_MEMORY_H */
