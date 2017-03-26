/*
 * base/arch/x86-64/fence.h - MainMemory arch-specific memory fences.
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

#ifndef BASE_ARCH_X86_64_FENCE_H
#define BASE_ARCH_X86_64_FENCE_H

#ifdef HAVE_XMMINTRIN_H

#include <xmmintrin.h>

#define mm_memory_strict_fence()	_mm_mfence()
#define mm_memory_strict_load_fence()	_mm_lfence()
#define mm_memory_strict_store_fence()	_mm_sfence()

#else

#define mm_memory_strict_fence()	asm volatile("mfence" ::: "memory")
#define mm_memory_strict_load_fence()	asm volatile("lfence" ::: "memory")
#define mm_memory_strict_store_fence()	asm volatile("sfence" ::: "memory")

#endif

#define mm_memory_fence()		mm_compiler_barrier()

#endif /* BASE_ARCH_X86_64_FENCE_H */
