/*
 * wait.h - MainMemory wait queue.
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

#ifndef WAIT_H
#define WAIT_H

#include "common.h"
#include "list.h"
#include "lock.h"

/* Forward declaration. */
struct mm_task;

/* An entry for a waiting task. */
struct mm_wait
{
	struct mm_link link;
	struct mm_task *task;
};

/* A cache of free wait entries. */
struct mm_wait_cache
{
	/* The cache of free wait entries. */
	struct mm_link cache;
	/* The cache of busy wait entries. */
	struct mm_link pending;
	/* The number of free entries in the wait cache. */
	uint32_t cache_size;
	/* The number of busy entries in the wait cache. */
	uint32_t pending_count;
};

/* A set of tasks waiting on an entity shared between cores. */
struct mm_waitset
{
	/* The task queue. */
	struct mm_link set;
	/* The number of entries in the queue. */
	uint32_t size;
};

void mm_wait_init(void);
void mm_wait_term(void);

void mm_wait_cache_prepare(struct mm_wait_cache *cache)
	__attribute__((nonnull(1)));
void mm_wait_cache_cleanup(struct mm_wait_cache *cache)
	__attribute__((nonnull(1)));
void mm_wait_cache_truncate(struct mm_wait_cache *cache)
	__attribute__((nonnull(1)));

void mm_waitset_prepare(struct mm_waitset *waitset)
	__attribute__((nonnull(1)));
void mm_waitset_cleanup(struct mm_waitset *waitset)
	__attribute__((nonnull(1)));

void mm_waitset_wait(struct mm_waitset *waitset, mm_core_lock_t *lock)
	__attribute__((nonnull(1)));

void mm_waitset_timedwait(struct mm_waitset *waitset, mm_core_lock_t *lock, mm_timeout_t timeout)
	__attribute__((nonnull(1)));

void mm_waitset_broadcast(struct mm_waitset *waitset, mm_core_lock_t *lock)
	__attribute__((nonnull(1)));

#endif /* WAIT_H */
