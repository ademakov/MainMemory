/*
 * base/lock.h - MainMemory locks.
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

#ifndef BASE_LOCK_H
#define BASE_LOCK_H

#include "common.h"
#include "arch/lock.h"
#include "base/thread/backoff.h"

/**********************************************************************
 * Basic TAS(TATAS) Spin Locks.
 **********************************************************************/

/*
 * The global lock and unlock functions should only be used to protect
 * few key global data structures.
 */

static inline void
mm_global_lock(mm_lock_t *lock)
{
	uint32_t backoff = 0;
	while (mm_lock_acquire(lock)) {
		do
			backoff = mm_thread_backoff(backoff);
		while (mm_memory_load(lock->locked));
	}
}

static inline void
mm_global_unlock(mm_lock_t *lock)
{
	mm_lock_release(lock);
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

struct mm_lock_stat *mm_lock_getstat(struct mm_lock_stat_info *info)
	__attribute__((nonnull(1)));

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
	bool fail = mm_lock_acquire(&lock->lock);

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

	while (mm_lock_acquire(&lock->lock)) {
		do {
#if ENABLE_LOCK_STATS
			++fail;
#endif
			backoff = mm_thread_backoff(backoff);
		} while (mm_memory_load(lock->lock.locked));
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
	mm_lock_release(&lock->lock);
}

static inline bool
mm_common_is_locked(mm_common_lock_t *lock)
{
	return mm_lock_is_acquired(&lock->lock);
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

static inline bool
mm_regular_is_locked(mm_regular_lock_t *lock)
{
#if ENABLE_SMP
	return mm_common_is_locked(&lock->lock);
#else
	(void) lock;
	return false;
#endif
}

/**********************************************************************
 * Lock statistics.
 **********************************************************************/

void mm_lock_stats(void);

#endif /* BASE_LOCK_H */
