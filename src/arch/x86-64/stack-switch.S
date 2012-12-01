/*
 * stack_switch.S - MainMemory arch-specific stack support.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "asm.h"

.text

PROC_ENTRY(C_NAME(mm_stack_switch))
PROC_START
	movq %rbp, -8(%rsp)
	movq %rbx, -16(%rsp)
	movq %r12, -24(%rsp)
	movq %r13, -32(%rsp)
	movq %r14, -40(%rsp)
	movq %r15, -48(%rsp)
	movq %rsp, (%rsi)
	movq %rdi, %rsp
	movq -8(%rsp), %rbp
	movq -16(%rsp), %rbx
	movq -24(%rsp), %r12
	movq -32(%rsp), %r13
	movq -40(%rsp), %r14
	movq -48(%rsp), %r15
	ret
PROC_END
