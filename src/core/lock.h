/*
 * core/lock.h - MainMemory task-only spin locks.
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

#ifndef CORE_LOCK_H
#define CORE_LOCK_H

#include "base/lock.h"

#if ENABLE_SMP
# define MM_TASK_LOCK_INIT	{ .lock = MM_THREAD_LOCK_INIT }
#else
# define MM_TASK_LOCK_INIT	{ .lock = 0 }
#endif

typedef struct
{
#if ENABLE_SMP
	mm_thread_lock_t lock;
#else
	uint8_t lock;
#endif

} mm_task_lock_t;

static inline bool
mm_task_trylock(mm_task_lock_t *lock)
{
#if ENABLE_SMP
	return mm_thread_trylock(&lock->lock);
#else
	(void) lock;
	return true;
#endif
}

static inline void
mm_task_lock(mm_task_lock_t *lock)
{
#if ENABLE_SMP
	mm_thread_lock(&lock->lock);
#else
	(void) lock;
#endif
}

static inline void
mm_task_unlock(mm_task_lock_t *lock)
{
#if ENABLE_SMP
	mm_thread_unlock(&lock->lock);
#else
	(void) lock;
#endif
}

static inline bool
mm_task_is_locked(mm_task_lock_t *lock)
{
#if ENABLE_SMP
	return mm_thread_is_locked(&lock->lock);
#else
	(void) lock;
	return false;
#endif
}

#endif /* CORE_LOCK_H */
