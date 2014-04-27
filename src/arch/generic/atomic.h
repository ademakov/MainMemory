/*
 * arch/generic/atomic.h - MainMemory arch-specific atomic ops.
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

#ifndef ARCH_GENERIC_ATOMIC_H
#define ARCH_GENERIC_ATOMIC_H

/**********************************************************************
 * Atomic types.
 **********************************************************************/

#define mm_atomic_type(base) base __align(sizeof(base))

typedef mm_atomic_type(uint8_t) mm_atomic_uint8_t;
typedef mm_atomic_type(uint16_t) mm_atomic_uint16_t;
typedef mm_atomic_type(uint32_t) mm_atomic_uint32_t;
typedef mm_atomic_type(uintptr_t) mm_atomic_uintptr_t;

/**********************************************************************
 * Atomic compare-and-swap operations.
 **********************************************************************/

#define mm_atomic_cas(base)						\
	static inline base##_t						\
	mm_atomic_##base##_cas(mm_atomic_##base##_t *p,			\
			       base##_t c, base##_t v)			\
	{								\
		return __sync_val_compare_and_swap(p, c, v);		\
	}

mm_atomic_cas(uint8)
mm_atomic_cas(uint16)
mm_atomic_cas(uint32)
mm_atomic_cas(uintptr)

#undef mm_atomic_cas

/**********************************************************************
 * Atomic arithmetics.
 **********************************************************************/

#define mm_atomic_fetch(base, name, func)				\
	static inline base##_t						\
	mm_atomic_##base##_fetch_and_##name(mm_atomic_##base##_t *p,	\
					    base##_t v)			\
	{								\
		return func(p, v);					\
	}

#define mm_atomic_unary(base, name, func)				\
	static inline void						\
	mm_atomic_##base##_##name(mm_atomic_##base##_t *p)		\
	{								\
		func(p, 1);					\
	}

#define mm_atomic_unary_test(base, name, func)				\
	static inline bool						\
	mm_atomic_##base##_##name##_and_test(mm_atomic_##base##_t *p)	\
	{								\
		return !func(p, 1);				\
	}

/* Define atomic fetch-and-set ops. */
mm_atomic_fetch(uint8, set, __sync_lock_test_and_set)
mm_atomic_fetch(uint16, set, __sync_lock_test_and_set)
mm_atomic_fetch(uint32, set, __sync_lock_test_and_set)
mm_atomic_fetch(uintptr, set, __sync_lock_test_and_set)

/* Define atomic fetch-and-add ops. */
mm_atomic_fetch(uint8, add, __sync_fetch_and_add)
mm_atomic_fetch(uint16, add, __sync_fetch_and_add)
mm_atomic_fetch(uint32, add, __sync_fetch_and_add)
mm_atomic_fetch(uintptr, add, __sync_fetch_and_add)

/* Define atomic increment ops. */
mm_atomic_unary(uint8, inc, __sync_fetch_and_add)
mm_atomic_unary(uint16, inc, __sync_fetch_and_add)
mm_atomic_unary(uint32, inc, __sync_fetch_and_add)
mm_atomic_unary(uintptr, inc, __sync_fetch_and_add)
mm_atomic_unary_test(uint8, inc, __sync_add_and_fetch)
mm_atomic_unary_test(uint16, inc, __sync_add_and_fetch)
mm_atomic_unary_test(uint32, inc, __sync_add_and_fetch)
mm_atomic_unary_test(uintptr, inc, __sync_add_and_fetch)

/* Define atomic decrement ops. */
mm_atomic_unary(uint8, dec, __sync_fetch_and_sub)
mm_atomic_unary(uint16, dec, __sync_fetch_and_sub)
mm_atomic_unary(uint32, dec, __sync_fetch_and_sub)
mm_atomic_unary(uintptr, dec, __sync_fetch_and_sub)
mm_atomic_unary_test(uint8, dec, __sync_sub_and_fetch)
mm_atomic_unary_test(uint16, dec, __sync_sub_and_fetch)
mm_atomic_unary_test(uint32, dec, __sync_sub_and_fetch)
mm_atomic_unary_test(uintptr, dec, __sync_sub_and_fetch)

#undef mm_atomic_fetch
#undef mm_atomic_unary
#undef mm_atomic_unary_test

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
	return __sync_lock_test_and_set(&lock->locked, 1);
}

static inline void
mm_atomic_lock_release(mm_atomic_lock_t *lock)
{
	__sync_lock_release(&lock->locked);
}

static inline void
mm_atomic_lock_pause(void)
{
}

#endif /* ARCH_GENERIC_ATOMIC_H */
