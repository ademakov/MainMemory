/*
 * base/memory/space.h - MainMemory memory spaces.
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

#ifndef BASE_MEMORY_SPACE_H
#define BASE_MEMORY_SPACE_H

#include "common.h"
#include "base/lock.h"
#include "base/log/error.h"
#include "base/memory/arena.h"

/* Forward declarations. */
struct mm_ring_spsc;

/**********************************************************************
 * Low-level memory space routines.
 **********************************************************************/

typedef struct {
	void *opaque;
} mm_mspace_t;

size_t
mm_mspace_getallocsize(const void *ptr);

size_t
mm_mspace_getfootprint(mm_mspace_t space);

size_t
mm_mspace_getfootprint_limit(mm_mspace_t space);

size_t
mm_mspace_setfootprint_limit(mm_mspace_t space, size_t size);

/**********************************************************************
 * Private memory space.
 **********************************************************************/

struct mm_private_space
{
	/* The underlying memory space. */
	mm_mspace_t space;

	/* Memory arena without error checking (using *_alloc family). */
	struct mm_arena uarena;
	/* Memory arena with error checking (using *_xalloc family). */
	struct mm_arena xarena;

	/* Memory chunks asynchronously released by outside threads. */
	struct mm_ring_spsc *reclaim_queue;
};

void NONNULL(1)
mm_private_space_prepare(struct mm_private_space *space, uint32_t queue_size);

void NONNULL(1)
mm_private_space_cleanup(struct mm_private_space *space);

static inline bool NONNULL(1)
mm_private_space_ready(struct mm_private_space *space)
{
	return space->space.opaque != NULL;
}

static inline void NONNULL(1)
mm_private_space_reset(struct mm_private_space *space)
{
	space->space.opaque = NULL;
}

void * NONNULL(1) MALLOC
mm_private_space_alloc(struct mm_private_space *space, size_t size);

void * NONNULL(1) MALLOC
mm_private_space_xalloc(struct mm_private_space *space, size_t size);

void * NONNULL(1) MALLOC
mm_private_space_aligned_alloc(struct mm_private_space *space, size_t align, size_t size);

void * NONNULL(1) MALLOC
mm_private_space_aligned_xalloc(struct mm_private_space *space, size_t align, size_t size);

void * NONNULL(1) MALLOC
mm_private_space_calloc(struct mm_private_space *space, size_t count, size_t size);

void * NONNULL(1) MALLOC
mm_private_space_xcalloc(struct mm_private_space *space, size_t count, size_t size);

void * NONNULL(1)
mm_private_space_realloc(struct mm_private_space *space, void *ptr, size_t size);

void * NONNULL(1)
mm_private_space_xrealloc(struct mm_private_space *space, void *ptr, size_t size);

void NONNULL(1)
mm_private_space_free(struct mm_private_space *space, void *ptr);

void NONNULL(1, 2)
mm_private_space_bulk_free(struct mm_private_space *space, void **ptrs, size_t nptrs);

void NONNULL(1)
mm_private_space_trim(struct mm_private_space *space);

bool NONNULL(1, 2)
mm_private_space_enqueue(struct mm_private_space *space, void *ptr);

bool NONNULL(1)
mm_private_space_reclaim(struct mm_private_space *space);

/**********************************************************************
 * Shared memory space.
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
	mm_common_lock_t lock;
};

void NONNULL(1)
mm_shared_space_prepare(struct mm_shared_space *space);

void NONNULL(1)
mm_shared_space_cleanup(struct mm_shared_space *space);

static inline bool NONNULL(1)
mm_shared_space_ready(struct mm_shared_space *space)
{
	return space->space.opaque != NULL;
}

static inline void NONNULL(1)
mm_shared_space_reset(struct mm_shared_space *space)
{
	space->space.opaque = NULL;
}

void * NONNULL(1) MALLOC
mm_shared_space_alloc(struct mm_shared_space *space, size_t size);

void * NONNULL(1) MALLOC
mm_shared_space_xalloc(struct mm_shared_space *space, size_t size);

void * NONNULL(1) MALLOC
mm_shared_space_aligned_alloc(struct mm_shared_space *space, size_t align, size_t size);

void * NONNULL(1) MALLOC
mm_shared_space_aligned_xalloc(struct mm_shared_space *space, size_t align, size_t size);

void * NONNULL(1) MALLOC
mm_shared_space_calloc(struct mm_shared_space *space, size_t count, size_t size);

void * NONNULL(1) MALLOC
mm_shared_space_xcalloc(struct mm_shared_space *space, size_t count, size_t size);

void * NONNULL(1)
mm_shared_space_realloc(struct mm_shared_space *space, void *ptr, size_t size);

void * NONNULL(1)
mm_shared_space_xrealloc(struct mm_shared_space *space, void *ptr, size_t size);

void NONNULL(1)
mm_shared_space_free(struct mm_shared_space *space, void *ptr);

void NONNULL(1, 2)
mm_shared_space_bulk_free(struct mm_shared_space *space, void **ptrs, size_t nptrs);

void NONNULL(1)
mm_shared_space_trim(struct mm_shared_space *space);

/**********************************************************************
 * Memory space subsystem initialization.
 **********************************************************************/

void
mm_space_init();

#endif /* BASE_MEMORY_SPACE_H */
