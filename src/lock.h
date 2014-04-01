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

/**********************************************************************
 * Synchronization between tasks running on different cores.
 **********************************************************************/

#define MM_TASK_LOCK_INIT { MM_ATOMIC_LOCK_INIT }

typedef struct { mm_atomic_lock_t lock; } mm_task_lock_t;

static inline bool
mm_task_trylock(mm_task_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);

#if ENABLE_SMP
	return !mm_atomic_lock_acquire(&lock->lock);
#else
	(void) lock;
	return true;
#endif
}

static inline uint32_t
mm_task_backoff(uint32_t count)
{
	ASSERT(mm_running_task != NULL);

	uint32_t n = count;
	if (n > 0x100) {
		mm_task_yield();
		n = 0x100;
	}

	while (n--)
		mm_atomic_lock_pause();

	return count ? count * 2 : 1;
}

static inline void
mm_task_lock(mm_task_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);
#if ENABLE_SMP
	uint32_t count = 0;
	while (mm_atomic_lock_acquire(&lock->lock)) {
		do
			count = mm_task_backoff(count);
		while (mm_memory_load(lock->lock.locked));
	}
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
	uint32_t n = count;
	if (n > 0x100) {
#if ENABLE_SMP
		if (mm_running_task != NULL)
			mm_task_yield();
#endif
		if (n < 0x1000) {
			n = 0x100;
		} else {
			mm_thread_yield();
			n = 0;
		}
	}

	while (n--)
		mm_atomic_lock_pause();

	return count ? count * 2 : 1;
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
