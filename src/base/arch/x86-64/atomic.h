/*
 * base/arch/x86-64/atomic.h - MainMemory arch-specific atomic ops.
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

#ifndef BASE_ARCH_X86_64_ATOMIC_H
#define BASE_ARCH_X86_64_ATOMIC_H

#define MM_LOCK_PREFIX "lock;"

/**********************************************************************
 * Atomic types.
 **********************************************************************/

#define mm_atomic_type(base) base ALIGN(sizeof(base))

typedef mm_atomic_type(uint8_t) mm_atomic_uint8_t;
typedef mm_atomic_type(uint16_t) mm_atomic_uint16_t;
typedef mm_atomic_type(uint32_t) mm_atomic_uint32_t;
typedef mm_atomic_type(uint64_t) mm_atomic_uint64_t;
typedef mm_atomic_type(uintptr_t) mm_atomic_uintptr_t;
typedef mm_atomic_type(void *) mm_atomic_ptr_t;

/**********************************************************************
 * Atomic compare-and-swap operations.
 **********************************************************************/

#define mm_atomic_cas_type(type, base, mnemonic, operand)		\
	static inline type						\
	mm_atomic_##base##_cas(mm_atomic_##base##_t *p, type c, type v)	\
	{								\
		type r;							\
		asm volatile(MM_LOCK_PREFIX mnemonic " %2,%1"		\
			     : "=a"(r), "+m"(*p)			\
			     : operand(v), "0"(c)			\
			     : "memory", "cc");				\
		return r;						\
	}

#define mm_atomic_cas(base, mnemonic, operand)				\
	mm_atomic_cas_type(base##_t, base, mnemonic, operand)

/* Define atomic compare-and-swap ops. */
mm_atomic_cas(uint8, "cmpxchgb", "q")
mm_atomic_cas(uint16, "cmpxchgw", "r")
mm_atomic_cas(uint32, "cmpxchgl", "r")
mm_atomic_cas(uint64, "cmpxchgq", "r")
mm_atomic_cas(uintptr, "cmpxchgq", "r")
mm_atomic_cas_type(void *, ptr, "cmpxchgq", "r")

#undef mm_atomic_cas_type
#undef mm_atomic_cas

/**********************************************************************
 * Atomic arithmetics.
 **********************************************************************/

#define mm_atomic_fetch_type(type, base, name, lock, mnemonic, operand)	\
	static inline type						\
	mm_atomic_##base##_fetch_and_##name(mm_atomic_##base##_t *p,	\
					    type v)			\
	{								\
		type r;							\
		asm volatile(lock mnemonic " %0,%1"			\
			     : "="operand(r), "+m"(*p)			\
			     : "0"(v)					\
			     : "memory");				\
		return r;						\
	}

#define mm_atomic_fetch(base, name, lock, mnemonic, operand)		\
	mm_atomic_fetch_type(base##_t, base, name, lock, mnemonic, operand)

#define mm_atomic_unary(base, name, mnemonic)				\
	static inline void						\
	mm_atomic_##base##_##name(mm_atomic_##base##_t *p)		\
	{								\
		asm volatile(MM_LOCK_PREFIX mnemonic " %0"		\
			     : "+m"(*p)					\
			     :						\
			     : "memory", "cc");				\
	}

#if __GCC_ASM_FLAG_OUTPUTS__
#define mm_atomic_unary_test(base, name, mnemonic)			\
	static inline int						\
	mm_atomic_##base##_##name##_and_test(mm_atomic_##base##_t *p)	\
	{								\
		char r;							\
		asm volatile(MM_LOCK_PREFIX mnemonic " %0"		\
			     : "+m"(*p), "=@ccnz" (r)			\
			     :						\
			     : "memory");				\
		return r;						\
	}
#else
#define mm_atomic_unary_test(base, name, mnemonic)			\
	static inline int						\
	mm_atomic_##base##_##name##_and_test(mm_atomic_##base##_t *p)	\
	{								\
		char r;							\
		asm volatile(MM_LOCK_PREFIX mnemonic " %0; setnz %1"	\
			     : "+m"(*p), "=qm" (r)			\
			     :						\
			     : "memory", "cc");				\
		return r;						\
	}
#endif

/* Define atomic fetch-and-set ops. */
mm_atomic_fetch(uint8, set, "", "xchgb", "q")
mm_atomic_fetch(uint16, set, "", "xchgw", "r")
mm_atomic_fetch(uint32, set, "", "xchgl", "r")
mm_atomic_fetch(uint64, set, "", "xchgq", "r")
mm_atomic_fetch(uintptr, set, "", "xchgq", "r")
mm_atomic_fetch_type(void *, ptr, set, "", "xchgq", "r")

/* Define atomic fetch-and-add ops. */
mm_atomic_fetch(uint8, add, MM_LOCK_PREFIX, "xaddb", "q")
mm_atomic_fetch(uint16, add, MM_LOCK_PREFIX, "xaddw", "r")
mm_atomic_fetch(uint32, add, MM_LOCK_PREFIX, "xaddl", "r")
mm_atomic_fetch(uint64, add, MM_LOCK_PREFIX, "xaddq", "r")
mm_atomic_fetch(uintptr, add, MM_LOCK_PREFIX, "xaddq", "r")
mm_atomic_fetch_type(void *, ptr, add, MM_LOCK_PREFIX, "xaddq", "r")

/* Define atomic increment ops. */
mm_atomic_unary(uint8, inc, "incb")
mm_atomic_unary(uint16, inc, "incw")
mm_atomic_unary(uint32, inc, "incl")
mm_atomic_unary(uint64, inc, "incq")
mm_atomic_unary(uintptr, inc, "incq")
mm_atomic_unary_test(uint8, inc, "incb")
mm_atomic_unary_test(uint16, inc, "incw")
mm_atomic_unary_test(uint32, inc, "incl")
mm_atomic_unary_test(uint64, inc, "incq")
mm_atomic_unary_test(uintptr, inc, "incq")

/* Define atomic decrement ops. */
mm_atomic_unary(uint8, dec, "decb")
mm_atomic_unary(uint16, dec, "decw")
mm_atomic_unary(uint32, dec, "decl")
mm_atomic_unary(uint64, dec, "decq")
mm_atomic_unary(uintptr, dec, "decq")
mm_atomic_unary_test(uint8, dec, "decb")
mm_atomic_unary_test(uint16, dec, "decw")
mm_atomic_unary_test(uint32, dec, "decl")
mm_atomic_unary_test(uint64, dec, "decq")
mm_atomic_unary_test(uintptr, dec, "decq")

#undef mm_atomic_fetch_type
#undef mm_atomic_fetch
#undef mm_atomic_unary
#undef mm_atomic_unary_test

#endif /* BASE_ARCH_X86_64_ATOMIC_H */
