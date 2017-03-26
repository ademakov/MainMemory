/*
 * base/atomic.h - MainMemory arch-specific atomic ops.
 *
 * Copyright (C) 2013,2016  Aleksey Demakov
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

#ifndef BASE_ATOMIC_H
#define BASE_ATOMIC_H

#include "common.h"
#include "base/arch/intrinsic.h"

/**********************************************************************
 * Architecture-specific atomic routines.
 **********************************************************************/

#if ARCH_X86
# include "base/arch/x86/atomic.h"
# include "base/arch/generic/atomic64.h"
#elif ARCH_X86_64
# include "base/arch/x86-64/atomic.h"
#else
# include "base/arch/generic/atomic.h"
# include "base/arch/generic/atomic64.h"
#endif

/**********************************************************************
 * Architecture-specific memory ordering.
 **********************************************************************/

#ifndef mm_memory_strict_fence
#define mm_memory_strict_fence()	__sync_synchronize()
#endif

#ifndef mm_memory_strict_load_fence
#define mm_memory_strict_load_fence()	mm_memory_strict_fence()
#endif

#ifndef mm_memory_strict_store_fence
#define mm_memory_strict_store_fence()	mm_memory_strict_fence()
#endif

#ifndef mm_memory_fence
#define mm_memory_fence()		mm_memory_strict_fence()
#endif

#ifndef mm_memory_load_fence
#define mm_memory_load_fence()		mm_memory_fence()
#endif

#ifndef mm_memory_store_fence
#define mm_memory_store_fence()		mm_memory_fence()
#endif

/* Cache coherency fences, most archs are automatically coherent
 * so these macros might need to be redefined only in very weird
 * situations. Simple compiler fences will do. */
#ifndef mm_memory_load_cache
#define mm_memory_load_cache()		mm_compiler_barrier()
#endif
#ifndef mm_memory_store_cache
#define mm_memory_store_cache()		mm_compiler_barrier()
#endif

/* A simple load op w/o specific ordering requirements. */
#define mm_memory_load(x) ({			\
		mm_memory_load_cache();		\
		mm_volatile_load(x);		\
	})

/* A simple store op w/o specific ordering requirements. */
#define mm_memory_store(x, v) ({		\
		mm_volatile_store(x, v);	\
		mm_memory_store_cache();	\
		v;				\
	})

/**********************************************************************
 * Atomic load and store for 64-bit values.
 **********************************************************************/

static inline uint64_t
mm_atomic_uint64_load(mm_atomic_uint64_t *p)
{
#if MM_WORD_64BIT
	return mm_memory_load(*p);
#else
	return mm_atomic_uint64_cas(p, 0, 0);
#endif
}

static inline void
mm_atomic_uint64_store(mm_atomic_uint64_t *p, uint64_t v)
{
#if MM_WORD_64BIT
	mm_memory_store(*p, v);
#else
	uint64_t u = *p;
	while ((u = mm_atomic_uint64_cas(p, u, v)) != v)
		;
#endif
}

#endif /* BASE_ATOMIC_H */
