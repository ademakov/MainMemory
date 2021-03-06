/*
 * base/arch/x86-64/syscall.h - MainMemory system call ABI.
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

#ifndef BASE_ARCH_X86_64_SYSCALL_H
#define BASE_ARCH_X86_64_SYSCALL_H

#include "common.h"

void
mm_syscall_error(uintptr_t result);

#if __linux__

static inline intptr_t
mm_syscall_result(uintptr_t result)
{
	return result > (uintptr_t) -4096 ? mm_syscall_error(result), -1 : (intptr_t) result;
}

static inline intptr_t
mm_syscall_0(int n)
{
	uintptr_t result;

	__asm__ __volatile__("syscall"
			     : "=a"(result)
			     : "0"(n)
			     : "cc", "memory", "rcx", "r11");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_1(int n, uintptr_t a1)
{
	uintptr_t result;

	__asm__ __volatile__("syscall"
			     : "=a"(result)
			     : "0"(n), "D"(a1)
			     : "cc", "memory", "rcx", "r11");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_2(int n, uintptr_t a1, uintptr_t a2)
{
	uintptr_t result;

	__asm__ __volatile__("syscall"
			     : "=a"(result)
			     : "0"(n), "D"(a1), "S"(a2)
			     : "cc", "memory", "rcx", "r11");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_3(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	uintptr_t result;

	__asm__ __volatile__("syscall"
			     : "=a"(result)
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3)
			     : "cc", "memory", "rcx", "r11");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_4(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	uintptr_t result;

	register uintptr_t r4 __asm__("r10") = a4;
	__asm__ __volatile__("syscall"
			     : "=a"(result)
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r4)
			     : "cc", "memory", "rcx", "r11");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_5(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,
	     uintptr_t a5)
{
	uintptr_t result;

	register uintptr_t r4 __asm__("r10") = a4;
	register uintptr_t r5 __asm__("r8") = a5;
	__asm__ __volatile__("syscall"
			     : "=a"(result)
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r4), "r"(r5)
			     : "cc", "memory", "rcx", "r11");
	return mm_syscall_result(result);
}

static inline intptr_t
mm_syscall_6(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, intptr_t a4,
	     uintptr_t a5, uintptr_t a6)
{
	uintptr_t result;

	register uintptr_t r4 __asm__("r10") = a4;
	register uintptr_t r5 __asm__("r8") = a5;
	register uintptr_t r6 __asm__("r9") = a6;
	__asm__ __volatile__("syscall"
			     : "=a"(result)
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r4), "r"(r5), "r"(r6)
			     : "cc", "memory", "rcx", "r11");
	return mm_syscall_result(result);
}

#else /* !__linux__ */

# if __APPLE__
#  define MM_SYSCALL_N(n)	(n | (2 << 24))
# endif

#if __GCC_ASM_FLAG_OUTPUTS__
# define MM_SYSCALL_INST	"syscall"
# define MM_SYSCALL_OUTPUTS	"=a"(result), "=@ccc"(error)
# define MM_SYSCALL_CLOBBER	"memory", "rcx", "r11"
#else
# define MM_SYSCALL_INST	"syscall; setc %1"
# define MM_SYSCALL_OUTPUTS	"=a"(result), "=c"(error)
# define MM_SYSCALL_CLOBBER	"cc", "memory", "r11"
#endif

static inline intptr_t
mm_syscall_result(uintptr_t result, uint8_t error)
{
	return error ? mm_syscall_error(result), -1 : (intptr_t) result;
}

static inline intptr_t
mm_syscall_0(int n)
{
	uintptr_t result;
	uint8_t error;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : MM_SYSCALL_OUTPUTS
			     : "0"(n)
			     : MM_SYSCALL_CLOBBER);
	return mm_syscall_result(result, error);
}

static inline intptr_t
mm_syscall_1(int n, uintptr_t a1)
{
	uintptr_t result;
	uint8_t error;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : MM_SYSCALL_OUTPUTS
			     : "0"(n), "D"(a1)
			     : MM_SYSCALL_CLOBBER);
	return mm_syscall_result(result, error);
}

static inline intptr_t
mm_syscall_2(int n, uintptr_t a1, uintptr_t a2)
{
	uintptr_t result;
	uint8_t error;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : MM_SYSCALL_OUTPUTS
			     : "0"(n), "D"(a1), "S"(a2)
			     : MM_SYSCALL_CLOBBER);
	return mm_syscall_result(result, error);
}

static inline intptr_t
mm_syscall_3(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3)
{
	uintptr_t result;
	uint8_t error;

	__asm__ __volatile__(MM_SYSCALL_INST
			     : MM_SYSCALL_OUTPUTS
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3)
			     : MM_SYSCALL_CLOBBER);
	return mm_syscall_result(result, error);
}

static inline intptr_t
mm_syscall_4(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
	uintptr_t result;
	uint8_t error;

	register uintptr_t r4 __asm__("r10") = a4;
	__asm__ __volatile__(MM_SYSCALL_INST
			     : MM_SYSCALL_OUTPUTS
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r4)
			     : MM_SYSCALL_CLOBBER);
	return mm_syscall_result(result, error);
}

static inline intptr_t
mm_syscall_5(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4,
	     uintptr_t a5)
{
	uintptr_t result;
	uint8_t error;

	register uintptr_t r4 __asm__("r10") = a4;
	register uintptr_t r5 __asm__("r8") = a5;
	__asm__ __volatile__(MM_SYSCALL_INST
			     : MM_SYSCALL_OUTPUTS
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r4), "r"(r5)
			     : MM_SYSCALL_CLOBBER);
	return mm_syscall_result(result, error);
}

static inline intptr_t
mm_syscall_6(int n, uintptr_t a1, uintptr_t a2, uintptr_t a3, intptr_t a4,
	     uintptr_t a5, uintptr_t a6)
{
	uintptr_t result;
	uint8_t error;

	register uintptr_t r4 __asm__("r10") = a4;
	register uintptr_t r5 __asm__("r8") = a5;
	register uintptr_t r6 __asm__("r9") = a6;
	__asm__ __volatile__(MM_SYSCALL_INST
			     : MM_SYSCALL_OUTPUTS
			     : "0"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r4), "r"(r5), "r"(r6)
			     : MM_SYSCALL_CLOBBER);
	return mm_syscall_result(result, error);
}

#endif /* !__linux__ */

#endif /* BASE_ARCH_X86_64_SYSCALL_H */
