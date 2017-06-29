/*
 * base/fiber/wait.h - MainMemory wait queues.
 *
 * Copyright (C) 2013-2017  Aleksey Demakov
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

#ifndef BASE_FIBER_WAIT_H
#define BASE_FIBER_WAIT_H

#include "common.h"
#include "base/list.h"
#include "base/lock.h"

/* Forward declaration. */
struct mm_fiber;

/* A cache of free wait entries. */
struct mm_wait_cache
{
	/* The cache of free wait entries. */
	struct mm_stack cache;
	/* The cache of busy wait entries. */
	struct mm_stack pending;
	/* The number of free entries in the wait cache. */
	uint32_t cache_size;
};

/* A set of fibers waiting on an entity shared between threads. */
struct mm_waitset
{
	union
	{
		/* Queue of waiting fibers. */
		struct mm_stack set;
		/* A single waiting fiber. */
		struct mm_fiber *fiber;
	};
	/* The wait-set has single waiting fiber. */
	bool signal;
};

/**********************************************************************
 * Wait-set global data initialization and cleanup.
 **********************************************************************/

void mm_wait_init(void);

/**********************************************************************
 * Per-strand wait entry cache initialization and cleanup.
 **********************************************************************/

void NONNULL(1)
mm_wait_cache_prepare(struct mm_wait_cache *cache);

void NONNULL(1)
mm_wait_cache_cleanup(struct mm_wait_cache *cache);

void NONNULL(1)
mm_wait_cache_truncate(struct mm_wait_cache *cache);

/**********************************************************************
 * Shared inter-thread wait-sets with locking.
 **********************************************************************/

void NONNULL(1)
mm_waitset_prepare(struct mm_waitset *waitset);

void NONNULL(1, 2)
mm_waitset_wait(struct mm_waitset *waitset, mm_regular_lock_t *lock);

void NONNULL(1, 2)
mm_waitset_timedwait(struct mm_waitset *waitset, mm_regular_lock_t *lock, mm_timeout_t timeout);

void NONNULL(1, 2)
mm_waitset_broadcast(struct mm_waitset *waitset, mm_regular_lock_t *lock);

/**********************************************************************
 * Shared inter-thread wait-set with single waiter fiber.
 **********************************************************************/

void NONNULL(1)
mm_waitset_unique_prepare(struct mm_waitset *waitset);

void NONNULL(1)
mm_waitset_unique_wait(struct mm_waitset *waitset);

void NONNULL(1)
mm_waitset_unique_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout);

void NONNULL(1)
mm_waitset_unique_signal(struct mm_waitset *waitset);

#endif /* BASE_FIBER_WAIT_H */
