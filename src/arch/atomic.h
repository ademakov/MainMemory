/*
 * arch/atomic.h - MainMemory arch-specific atomic ops.
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

#ifndef ARCH_ATOMIC_H
#define ARCH_ATOMIC_H

#include "common.h"

#if ARCH_X86
# include "arch/x86/atomic.h"
# include "arch/generic/atomic64.h"
#elif ARCH_X86_64
# include "arch/x86-64/atomic.h"
#else
# include "arch/generic/atomic.h"
# include "arch/generic/atomic64.h"
#endif

#include "arch/memory.h"

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

#endif /* ARCH_ATOMIC_H */
