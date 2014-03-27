/*
 * lock.h - MainMemory locks.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#define MM_TASK_LOCK_INIT ((mm_task_lock_t) { MM_ATOMIC_LOCK_INIT })

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

static inline void
mm_task_lock(mm_task_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);
#if ENABLE_SMP
	register int count = 0;
	while (mm_atomic_lock_acquire(&lock->lock)) {
		do {
			mm_atomic_lock_pause();
			if ((count & 0x0f) == 0x0f) {
				if ((count & 0x3f) == 0x3f) {
					mm_thread_yield();
				} else {
					mm_task_yield();
				}
			}
			count++;
		} while (mm_memory_load(lock->lock.locked));
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

#define MM_THREAD_LOCK_INIT ((mm_thread_lock_t) { MM_ATOMIC_LOCK_INIT })

typedef struct { mm_atomic_lock_t lock; } mm_thread_lock_t;

static inline bool
mm_thread_trylock(mm_thread_lock_t *lock)
{
	return !mm_atomic_lock_acquire(&lock->lock);
}

static inline void
mm_thread_lock(mm_thread_lock_t *lock)
{
#if ENABLE_SMP
	register int count = 0;
	while (mm_atomic_lock_acquire(&lock->lock)) {
		do {
			mm_atomic_lock_pause();
			if ((count & 0x0f) == 0x0f) {
				if ((count & 0x3f) == 0x3f) {
					mm_thread_yield();
				} else if (mm_running_task != NULL) {
					mm_task_yield();
				}
			}
			count++;
		} while (mm_memory_load(lock->lock.locked));
	}
#else
	while (mm_atomic_lock_acquire(&lock->lock))
		mm_thread_yield();
#endif
}

static inline void
mm_thread_unlock(mm_thread_lock_t *lock)
{
	mm_atomic_lock_release(&lock->lock);
}

#endif /* LOCK_H */
