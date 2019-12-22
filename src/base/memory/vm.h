/*
 * base/memory/vm.h - MainMemory virtual memory allocator.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#ifndef BASE_MEMORY_VM_H
#define BASE_MEMORY_VM_H

#include "common.h"
#include "base/list.h"
#include "base/lock.h"
#include "base/memory/cache.h"

#define MM_VM_ARENA_SIZE	(104)
#define MM_VM_CACHE_SIZE	(72)

#define MM_VM_ALLOC_ZERO	(1)
#define MM_VM_ALLOC_FAST	(2)
#define MM_VM_ALLOC_CACHELINE	(4)

typedef uint32_t mm_vm_flags_t;

struct mm_vm_arena
{
	struct mm_stack chunks[MM_VM_ARENA_SIZE];
};

struct mm_vm_cache
{
	struct mm_vm_arena *arena;
	struct mm_stack chunks[MM_VM_CACHE_SIZE];
};

void NONNULL(1)
mm_vm_arena_prepare(struct mm_vm_arena *arena);

void NONNULL(1, 2)
mm_vm_cache_prepare(struct mm_vm_cache *cache, struct mm_vm_arena *arena);

void * NONNULL(1)
mm_vm_alloc(struct mm_vm_cache *cache, size_t size, mm_vm_flags_t flags);

void * NONNULL(1)
mm_vm_realloc(struct mm_vm_cache *cache, void *ptr, size_t size);

void NONNULL(1)
mm_vm_free(struct mm_vm_cache *cache, void *ptr);

#endif /* BASE_MEMORY_VM_H */
