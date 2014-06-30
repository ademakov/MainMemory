/*
 * arch/x86-64/lock.h - MainMemory test-and-set lock primitives.
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

#ifndef ARCH_X86_64_LOCK_H
#define ARCH_X86_64_LOCK_H

#define MM_LOCK_INIT	{0}

typedef struct { char locked; } mm_lock_t;

static inline int
mm_lock_acquire(mm_lock_t *lock)
{
	char locked;
	asm volatile("xchgb %0,%1"
		     : "=q"(locked), "+m"(lock->locked)
		     : "0"(1)
		     : "memory");
	return locked;
}

static inline void
mm_lock_release(mm_lock_t *lock)
{
	mm_memory_store_fence();
	mm_memory_store(lock->locked, 0);
}

#endif /* ARCH_X86_64_LOCK_H */
