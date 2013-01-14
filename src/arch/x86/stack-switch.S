/*
 * stack_switch.S - MainMemory arch-specific stack support.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

PROC_ENTRY(C_NAME(mm_stack_switch))
PROC_START
	movl 4(%esp), %ecx
	movl 8(%esp), %edx
	movl %ebp, -4(%esp)
	movl %ebx, -8(%esp)
	movl %esi, -12(%esp)
	movl %edi, -16(%esp)
	movl %esp, (%ecx)
	movl (%edx), %esp
	movl -4(%esp), %ebp
	movl -8(%esp), %ebx
	movl -12(%esp), %esi
	movl -16(%esp), %edi
	ret
PROC_END
