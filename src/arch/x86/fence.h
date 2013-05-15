/*
 * arch/x86/fence.h - MainMemory arch-specific memory fences.
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

#ifndef ARCH_X86_FENCE_H
#define ARCH_X86_FENCE_H

#define mm_memory_strict_fence()	asm volatile("lock; addl $0,0(%%esp)" ::: "memory")

#endif /* ARCH_X86_FENCE_H */
