/*
 * arch/memory.h - MainMemory arch-specific memory access macros.
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

#ifndef ARCH_MEMORY_H
#define ARCH_MEMORY_H

#include "config.h"

/**********************************************************************
 * Basic Properties.
 **********************************************************************/

/* Virtual memory page size. */
#define MM_PAGE_SIZE		(4096)

/* Cache line size. */
#define MM_CACHELINE		(64)

/**********************************************************************
 * Hardware Memory Ordering.
 **********************************************************************/

#if ARCH_X86
# include "arch/x86/fence.h"
#endif
#if ARCH_X86_64
# include "arch/x86-64/fence.h"
#endif

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

#endif /* ARCH_MEMORY_H */
