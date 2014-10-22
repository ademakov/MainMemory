/*
 * core/wait.h - MainMemory wait queues.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#ifndef CORE_WAIT_H
#define CORE_WAIT_H

#include "common.h"
#include "base/list.h"
#include "core/lock.h"

/* Forward declaration. */
struct mm_task;

/* A cache of free wait entries. */
struct mm_wait_cache
{
	/* The cache of free wait entries. */
	struct mm_link cache;
	/* The cache of busy wait entries. */
	struct mm_link pending;
	/* The number of free entries in the wait cache. */
	uint32_t cache_size;
};

/* A set of tasks waiting on an entity shared between cores. */
struct mm_waitset
{
	/* The task queue. */
	struct mm_link set;
	/* The core the waitset is pinned to. It is equal to
	   MM_CORE_NONE in case the waitset is not pinned. */
	mm_core_t core;
};

/**********************************************************************
 * Wait entry global data initialization and cleanup.
 **********************************************************************/

void mm_wait_init(void);
void mm_wait_term(void);

/**********************************************************************
 * Per-core wait entry cache initialization and cleanup.
 **********************************************************************/

void mm_wait_cache_prepare(struct mm_wait_cache *cache)
	__attribute__((nonnull(1)));
void mm_wait_cache_cleanup(struct mm_wait_cache *cache)
	__attribute__((nonnull(1)));
void mm_wait_cache_truncate(struct mm_wait_cache *cache)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Wait-set initialization and cleanup.
 **********************************************************************/

void mm_waitset_prepare(struct mm_waitset *waitset)
	__attribute__((nonnull(1)));
void mm_waitset_cleanup(struct mm_waitset *waitset)
	__attribute__((nonnull(1)));

static inline void
mm_waitset_pin(struct mm_waitset *waitset, mm_core_t core)
{
	waitset->core = core;
}

/**********************************************************************
 * Private single-core wait-sets.
 **********************************************************************/

void mm_waitset_local_wait(struct mm_waitset *waitset)
	__attribute__((nonnull(1)));

void mm_waitset_local_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout)
	__attribute__((nonnull(1)));

void mm_waitset_local_broadcast(struct mm_waitset *waitset)
	__attribute__((nonnull(1)));

/**********************************************************************
 * Shared inter-core wait-sets with locking.
 **********************************************************************/

void mm_waitset_wait(struct mm_waitset *waitset, mm_task_lock_t *lock)
	__attribute__((nonnull(1, 2)));

void mm_waitset_timedwait(struct mm_waitset *waitset, mm_task_lock_t *lock, mm_timeout_t timeout)
	__attribute__((nonnull(1, 2)));

void mm_waitset_broadcast(struct mm_waitset *waitset, mm_task_lock_t *lock)
	__attribute__((nonnull(1, 2)));

#endif /* CORE_WAIT_H */
