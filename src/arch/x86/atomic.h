/*
 * arch/x86/atomic.h - MainMemory arch-specific atomic ops.
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

#ifndef ARCH_X86_ATOMIC_H
#define ARCH_X86_ATOMIC_H

/**********************************************************************
 * Atomic types.
 **********************************************************************/

#define mm_atomic_type(base_type) \
	struct { base_type value __align(sizeof(base_type)); }

typedef mm_atomic_type(uint8_t) mm_atomic_8_t;
typedef mm_atomic_type(uint16_t) mm_atomic_16_t;
typedef mm_atomic_type(uint32_t) mm_atomic_32_t;

/**********************************************************************
 * Atomic arithmetics.
 **********************************************************************/

#define mm_atomic_unary(bits, name, mnemonic)			\
	static inline void					\
	mm_atomic_##bits##_##name(mm_atomic_##bits##_t *p)	\
	{							\
		asm volatile("lock; " mnemonic " %0"		\
			     : "+m"(p->value)			\
			     : 					\
			     : "memory", "cc");			\
	}

mm_atomic_unary(8, inc, "incb")
mm_atomic_unary(16, inc, "incw")
mm_atomic_unary(32, inc, "incl")
mm_atomic_unary(8, dec, "decb")
mm_atomic_unary(16, dec, "decw")
mm_atomic_unary(32, dec, "decl")

/**********************************************************************
 * Atomic operations for spin-locks.
 **********************************************************************/

/*
 * mm_atomic_lock_acquire() is a test-and-set atomic operation along with
 * acquire fence.
 * 
 * mm_atomic_lock_release() is a simple clear operation along with release
 * fence.
 * 
 * mm_atomic_lock_pause() is a special instruction to be used in spin-lock
 * loops to make hyper-threading CPUs happy.
 */

#define MM_ATOMIC_LOCK_INIT	{0}

typedef struct { char locked; } mm_atomic_lock_t;

static inline int
mm_atomic_lock_acquire(mm_atomic_lock_t *lock)
{
	char locked;
	asm volatile("xchgb %0, %1" : "=q"(locked), "+m"(lock->locked) : "0"(1) : "memory");
	return locked;
}

static inline void
mm_atomic_lock_release(mm_atomic_lock_t *lock)
{
	mm_memory_store_fence();
	(lock)->locked = 0;
}

static inline void
mm_atomic_lock_pause(void)
{
	asm volatile("rep; nop" ::: "memory");
}

#endif /* ARCH_X86_ATOMIC_H */
