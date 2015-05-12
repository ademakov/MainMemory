/*
 * core/wait.h - MainMemory wait queues.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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
	union
	{
		/* The task queue. */
		struct mm_link set;
		struct mm_task *task;
	};
	/* The core the wait-set is pinned to. It is equal to
	   MM_CORE_NONE in case the wait-set is not pinned. */
	mm_core_t core;
	/* The wait-set has single waiting task. */
	bool signal;
};

/**********************************************************************
 * Wait-set global data initialization and cleanup.
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
 * Shared inter-core wait-sets with locking.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_waitset_prepare(struct mm_waitset *waitset);

void __attribute__((nonnull(1, 2)))
mm_waitset_wait(struct mm_waitset *waitset, mm_task_lock_t *lock);

void __attribute__((nonnull(1, 2)))
mm_waitset_timedwait(struct mm_waitset *waitset, mm_task_lock_t *lock, mm_timeout_t timeout);

void __attribute__((nonnull(1, 2)))
mm_waitset_broadcast(struct mm_waitset *waitset, mm_task_lock_t *lock);

/**********************************************************************
 * Private single-core wait-sets.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_waitset_local_prepare(struct mm_waitset *waitset, mm_core_t core);

void __attribute__((nonnull(1)))
mm_waitset_local_wait(struct mm_waitset *waitset);

void __attribute__((nonnull(1)))
mm_waitset_local_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout);

void __attribute__((nonnull(1)))
mm_waitset_local_broadcast(struct mm_waitset *waitset);

/**********************************************************************
 * Shared inter-core wait-set with single waiter task.
 **********************************************************************/

void __attribute__((nonnull(1)))
mm_waitset_unique_prepare(struct mm_waitset *waitset);

void __attribute__((nonnull(1)))
mm_waitset_unique_wait(struct mm_waitset *waitset);

void __attribute__((nonnull(1)))
mm_waitset_unique_timedwait(struct mm_waitset *waitset, mm_timeout_t timeout);

void __attribute__((nonnull(1)))
mm_waitset_unique_signal(struct mm_waitset *waitset);

#endif /* CORE_WAIT_H */
