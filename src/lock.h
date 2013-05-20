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
#include "sched.h"
#include "thread.h"
#include "util.h"

/**********************************************************************
 * Synchronization between core threads.
 **********************************************************************/

typedef mm_atomic_lock_t mm_core_lock_t;

static inline void
mm_core_lock(mm_core_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);
#if ENABLE_SMP
	register int count = 0;
	while (mm_atomic_lock_acquire(lock)) {
		do {
			mm_atomic_lock_pause();
			if ((count & 0x0f) == 0x0f) {
				if ((count & 0x3f) == 0x3f) {
					mm_thread_yield();
				} else {
					mm_sched_yield();
				}
			}
			count++;
		} while (lock->locked);
	}
#else
	(void) lock;
#endif
}

static inline void
mm_core_unlock(mm_core_lock_t *lock)
{
	ASSERT(mm_running_task != NULL);
#if ENABLE_SMP
	mm_atomic_lock_release(lock);
#else
	(void) lock;
#endif
}

/**********************************************************************
 * Synchronization between core and auxiliary threads.
 **********************************************************************/

typedef mm_atomic_lock_t mm_global_lock_t;

static inline void
mm_global_lock(mm_core_lock_t *lock)
{
#if ENABLE_SMP
	register int count = 0;
	while (mm_atomic_lock_acquire(lock)) {
		do {
			mm_atomic_lock_pause();
			if ((count & 0x0f) == 0x0f) {
				if ((count & 0x3f) == 0x3f) {
					mm_thread_yield();
				} else if (mm_running_task != NULL) {
					mm_sched_yield();
				}
			}
			count++;
		} while (lock->locked);
	}
#else
	while (mm_atomic_lock_acquire(lock))
		mm_thread_yield();
#endif
}

static inline void
mm_global_unlock(mm_global_lock_t *lock)
{
	mm_atomic_lock_release(lock);
}

#endif /* LOCK_H */
