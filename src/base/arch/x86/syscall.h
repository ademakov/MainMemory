/*
 * base/arch/x86/syscall.h - MainMemory system call ABI.
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

#ifndef BASE_ARCH_X86_SYSCALL_H
#define BASE_ARCH_X86_SYSCALL_H

void __attribute__((__regparm__(1)))
mm_syscall_error(uintptr_t result);

#if __linux__

#if X86_SLOW_SYSCALL
# define MM_SYSCALL_INST	"int $0x80"
#else
# define MM_SYSCALL_INST	"call *%gs:0x10"
#endif

static inline intptr_t
mm_syscall_result(uintptr_t result)
{
	return result > (uintptr_t) -4096 ? mm_syscall_error(result), -1 : (intptr_t) result;
}

static inline intptr_t
mm_syscall_0(int n)
{
	uintptr_t result;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : "=a"(result)
			     : "0"(n)
			     : "cc", "memory");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_1(int n, uintptr_t a1)
{
	uintptr_t result;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : "=a"(result)
			     : "0"(n), "b"(a1)
			     : "cc", "memory");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_2(int n, uintptr_t a1, uintptr_t a2)
{
	uintptr_t result;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : "=a"(result)
			     : "0"(n), "b"(a1), "c"(a2)
			     : "cc", "memory");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_3(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	uintptr_t result;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : "=a"(result)
			     : "0"(n), "b"(a1), "c"(a2), "d"(a3)
			     : "cc", "memory");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_4(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	uintptr_t result;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : "=a"(result)
			     : "0"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
			     : "cc", "memory");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_5(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,
	     uintptr_t a5)
{
	uintptr_t result;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : "=a"(result)
			     : "0"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5)
			     : "cc", "memory");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_6(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, intptr_t a4,
	     uintptr_t a5, uintptr_t a6)
{
	uintptr_t result;

	register uintptr_t r6 __asm__("ebp") = a6;
	__asm__ __volatile__(MM_SYSCALL_INST
			     : "=a"(result)
			     : "0"(n), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5), "r"(r6)
			     : "cc", "memory");
	return mm_syscall_result(result);
}

#else /* !__linux__ */

# include "arch/generic/syscall.h"

#endif /* !__linux__ */

#endif /* BASE_ARCH_X86_SYSCALL_H */
