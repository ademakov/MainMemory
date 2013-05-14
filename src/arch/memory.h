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

/* Virtual memory page size. */
#define MM_PAGE_SIZE		(4096)

/* Cache line size. */
#define MM_CACHELINE		(64)

/* Cache coherency fences, most archs are automatically coherent
 * so these macros might need to be redefined only in very weird
 * situations. Simple compiler fences will do. */
#define mm_memory_cache_load()	mm_compiler_barrier()
#define mm_memory_cache_store()	mm_compiler_barrier()

#define mm_memory_load(x) ({			\
		mm_memory_cache_load();		\
		mm_volatile_load(x);		\
	})

#define mm_memory_store(x, v) ({		\
		mm_volatile_store(x, v);	\
		mm_memory_cache_store();	\
		v;				\
	})

#endif /* ARCH_MEMORY_H */
