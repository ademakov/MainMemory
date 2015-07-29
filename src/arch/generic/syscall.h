/*
 * arch/syscall.h - MainMemory system call ABI.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#ifndef ARCH_GENERIC_SYSCALL_H
#define ARCH_GENERIC_SYSCALL_H

#include <unistd.h>

static inline intptr_t
mm_syscall_0(int n)
{
	return syscall(n);
}

static inline intptr_t
mm_syscall_1(int n, uintptr_t a1)
{
	return syscall(n, a1);
}

static inline intptr_t
mm_syscall_2(int n, uintptr_t a1, uintptr_t a2)
{
	return syscall(n, a1, a2);
}

static inline intptr_t
mm_syscall_3(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	return syscall(n, a1, a2, a3);
}

static inline intptr_t
mm_syscall_4(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	return syscall(n, a1, a2, a3, a4);
}

static inline intptr_t
mm_syscall_5(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,
	     uintptr_t a5)
{
	return syscall(n, a1, a2, a3, a4, a5);
}

static inline intptr_t
mm_syscall_6(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, intptr_t a4,
	     uintptr_t a5, uintptr_t a6)
{
	return syscall(n, a1, a2, a3, a4, a5, a6);
}

#endif /* ARCH_GENERIC_SYSCALL_H */
