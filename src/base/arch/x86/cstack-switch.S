/*
 * base/arch/x86/cstack-switch.S - Arch-specific call stack support.
 *
 * Copyright (C) 2012-2019  Aleksey Demakov
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

#include "asm.h"

.text

PROC_ENTRY(C_NAME(mm_cstack_switch))
PROC_START
	movl 4(%esp), %ecx
	movl 8(%esp), %edx
	movl %esp, (%ecx)
	movl (%edx), %esp
	popl %eax
	movl %ebp, 4(%ecx)
	movl %ebx, 8(%ecx)
	movl %esi, 12(%ecx)
	movl %edi, 16(%ecx)
	movl 4(%edx), %ebp
	movl 8(%edx), %ebx
	movl 12(%edx), %esi
	movl 16(%edx), %edi
	jmpl *%eax
PROC_END
