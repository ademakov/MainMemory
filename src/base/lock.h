/*
 * base/lock.h - MainMemory locks.
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

#ifndef BASE_LOCK_H
#define BASE_LOCK_H

#include "common.h"
#include "base/atomic.h"
#include "base/thread/backoff.h"

/**********************************************************************
 * Basic TAS(TATAS) Spin Locks.
 **********************************************************************/

/*
 * The global lock and unlock functions should only be used to protect
 * few key global data structures.
 */

#define MM_LOCK_INIT	{0}

typedef struct { mm_atomic_uint8_t value; } mm_lock_t;

static inline void
mm_global_lock(mm_lock_t *lock)
{
	uint32_t backoff = 0;
	// TODO: FAS with acquire semantics.
	while (mm_atomic_uint8_fetch_and_set(&lock->value, 1)) {
		do
			backoff = mm_thread_backoff(backoff);
		while (mm_memory_load(lock->value));
	}
}

static inline void
mm_global_unlock(mm_lock_t *lock)
{
	// TODO: store with release semantics
	mm_memory_store_fence();
	mm_memory_store(lock->value, 0);
}

/**********************************************************************
 * Lock Statistics.
 **********************************************************************/

#if ENABLE_LOCK_STATS

#define MM_LOCK_STAT_INIT	{ .stat = NULL,			\
				  .location = __LOCATION__,	\
				  .moreinfo = NULL }

/* Per-thread statistics entry for a lock. */
struct mm_lock_stat
{
	uint64_t lock_count;
	uint64_t fail_count;
};

/* Collection of statistics entries for a lock for all threads. */
struct mm_lock_stat_set;

/* Entire statistics and identification information for a lock. */
struct mm_lock_stat_info
{
	struct mm_lock_stat_set *stat;

	/* Initialization location. */
	const char *location;

	/* Additional identification information. */
	const char *moreinfo;
};

struct mm_lock_stat * NONNULL(1)
mm_lock_getstat(struct mm_lock_stat_info *info);

#endif

/**********************************************************************
 * Extended TAS(TATAS) Spin Locks (with optional statistics).
 **********************************************************************/

#if ENABLE_LOCK_STATS
# define MM_COMMON_LOCK_INIT	{ .lock = MM_LOCK_INIT, .stat = MM_LOCK_STAT_INIT }
#else
# define MM_COMMON_LOCK_INIT	{ .lock = MM_LOCK_INIT }
#endif

typedef struct
{
	mm_lock_t lock;

#if ENABLE_LOCK_STATS
	struct mm_lock_stat_info stat;
#endif

} mm_common_lock_t;

static inline bool
mm_common_trylock(mm_common_lock_t *lock)
{
	// TODO: FAS with acquire semantics.
	bool fail = mm_atomic_uint8_fetch_and_set(&lock->lock.value, 1);

#if ENABLE_LOCK_STATS
	struct mm_lock_stat *stat = mm_lock_getstat(&lock->stat);
	if (fail)
		stat->fail_count++;
	else
		stat->lock_count++;
#endif

	return !fail;
}

static inline void
mm_common_lock(mm_common_lock_t *lock)
{
#if ENABLE_LOCK_STATS
	uint32_t fail = 0;
#endif
	uint32_t backoff = 0;

	// TODO: FAS with acquire semantics.
	while (mm_atomic_uint8_fetch_and_set(&lock->lock.value, 1)) {
		do {
#if ENABLE_LOCK_STATS
			++fail;
#endif
			backoff = mm_thread_backoff(backoff);
		} while (mm_memory_load(lock->lock.value));
	}

#if ENABLE_LOCK_STATS
	struct mm_lock_stat *stat = mm_lock_getstat(&lock->stat);
	stat->fail_count += fail;
	stat->lock_count++;
#endif
}

static inline void
mm_common_unlock(mm_common_lock_t *lock)
{
	// TODO: store with release semantics
	mm_memory_store_fence();
	mm_memory_store(lock->lock.value, 0);
}

/**********************************************************************
 * Spin Locks for Regular Threads.
 **********************************************************************/

/*
 * NB: This code implies there is only one regular thread in non-SMP mode.
 */

#if ENABLE_SMP
# define MM_REGULAR_LOCK_INIT	{ .lock = MM_COMMON_LOCK_INIT }
#else
# define MM_REGULAR_LOCK_INIT	{ .lock = 0 }
#endif

typedef struct
{
#if ENABLE_SMP
	mm_common_lock_t lock;
#else
	uint8_t lock;
#endif

} mm_regular_lock_t;

static inline bool
mm_regular_trylock(mm_regular_lock_t *lock)
{
#if ENABLE_SMP
	return mm_common_trylock(&lock->lock);
#else
	(void) lock;
	return true;
#endif
}

static inline void
mm_regular_lock(mm_regular_lock_t *lock)
{
#if ENABLE_SMP
	mm_common_lock(&lock->lock);
#else
	(void) lock;
#endif
}

static inline void
mm_regular_unlock(mm_regular_lock_t *lock)
{
#if ENABLE_SMP
	mm_common_unlock(&lock->lock);
#else
	(void) lock;
#endif
}

/**********************************************************************
 * Lock statistics.
 **********************************************************************/

void mm_lock_stats(void);

#endif /* BASE_LOCK_H */
