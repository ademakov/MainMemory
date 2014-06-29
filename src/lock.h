/*
 * lock.h - MainMemory locks.
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

#ifndef LOCK_H
#define LOCK_H

#include "common.h"
#include "task.h"
#include "thread.h"
#include "trace.h"

#if ENABLE_LOCK_STATS
#include "cdata.h"
#include "list.h"
#endif

/**********************************************************************
 * Synchronization between tasks running on different cores.
 **********************************************************************/

#if ENABLE_LOCK_STATS
# define MM_TASK_LOCK_INIT	{ .lock = MM_ATOMIC_LOCK_INIT, .stat = NULL, .file = __FILE__, .line = __LINE__ }
#else
# define MM_TASK_LOCK_INIT	{ .lock = MM_ATOMIC_LOCK_INIT }
#endif

#if ENABLE_LOCK_STATS

/* Lock statistics for all cores. */
struct mm_task_lock_statistics;

/* Lock statistics for single core. */
struct mm_task_lock_core_stat
{
	uint64_t lock_count;
	uint64_t fail_count;
};

#endif

typedef struct
{
	mm_atomic_lock_t lock;

#if ENABLE_LOCK_STATS
	struct mm_task_lock_statistics *stat;
	const char *file;
	int line;
#endif

} mm_task_lock_t;


#if ENABLE_LOCK_STATS
struct mm_task_lock_core_stat *mm_task_lock_getstat(mm_task_lock_t *lock)
	__attribute__((nonnull(1)));
#endif

static inline bool
mm_task_trylock(mm_task_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);

#if ENABLE_SMP
	bool success = !mm_atomic_lock_acquire(&lock->lock);

# if ENABLE_LOCK_STATS
	struct mm_task_lock_core_stat *stat = mm_task_lock_getstat(lock);
	if (success)
		stat->lock_count++;
	else
		stat->fail_count++;
#endif

	return success;
#else
	(void) lock;
	return true;
#endif
}

static inline uint32_t
mm_task_backoff(uint32_t count)
{
	ASSERT(mm_running_task != NULL);

	if (count > 0xff) {
		count = 0;
		mm_task_yield();
	}

	for (uint32_t n = count; n; n--)
		mm_atomic_lock_pause();

	return count * 2 + 1;
}

static inline void
mm_task_lock(mm_task_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);

#if ENABLE_SMP

# if ENABLE_LOCK_STATS
	uint32_t fail = 0;
# endif
	uint32_t backoff = 0;

	while (mm_atomic_lock_acquire(&lock->lock)) {
		do {
# if ENABLE_LOCK_STATS
			++fail;
# endif
			backoff = mm_task_backoff(backoff);
		} while (mm_memory_load(lock->lock.locked));
	}

# if ENABLE_LOCK_STATS
	struct mm_task_lock_core_stat *stat = mm_task_lock_getstat(lock);
	stat->fail_count += fail;
	stat->lock_count++;
# endif

#else
	(void) lock;
#endif
}

static inline void
mm_task_unlock(mm_task_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);

#if ENABLE_SMP
	mm_atomic_lock_release(&lock->lock);
#else
	(void) lock;
#endif
}

/**********************************************************************
 * Synchronization between different threads.
 **********************************************************************/

#define MM_THREAD_LOCK_INIT { MM_ATOMIC_LOCK_INIT }

typedef struct { mm_atomic_lock_t lock; } mm_thread_lock_t;

static inline bool
mm_thread_trylock(mm_thread_lock_t *lock)
{
	return !mm_atomic_lock_acquire(&lock->lock);
}

static inline uint32_t
mm_thread_backoff(uint32_t count)
{
#if ENABLE_SMP
	if (count > 0xff) {
		if (count > 0x7ff) {
			count = 0;
			mm_thread_yield();
		} else if (mm_running_task != NULL) {
			mm_task_yield();
		}
	}
#else
	if (count > 0x7ff) {
		count = 0;
		mm_thread_yield();
	}
#endif

	for (uint32_t n = count & 0xff; n; n--)
		mm_atomic_lock_pause();

	return count * 2 + 1;
}

static inline void
mm_thread_lock(mm_thread_lock_t *lock)
{
	uint32_t count = 0;
	while (mm_atomic_lock_acquire(&lock->lock)) {
		do
			count = mm_thread_backoff(count);
		while (mm_memory_load(lock->lock.locked));
	}
}

static inline void
mm_thread_unlock(mm_thread_lock_t *lock)
{
	mm_atomic_lock_release(&lock->lock);
}

#endif /* LOCK_H */
