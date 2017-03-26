/*
 * base/arch/x86-64/cstack-switch.S - Arch-specific call stack support.
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

PROC_ENTRY(C_NAME(mm_cstack_switch))
PROC_START
	movq %rbp, -8(%rsp)
	movq %rbx, -16(%rsp)
	movq %r12, -24(%rsp)
	movq %r13, -32(%rsp)
	movq %r14, -40(%rsp)
	movq %r15, -48(%rsp)
	movq %rsp, (%rdi)
	movq (%rsi), %rsp
	movq -8(%rsp), %rbp
	movq -16(%rsp), %rbx
	movq -24(%rsp), %r12
	movq -32(%rsp), %r13
	movq -40(%rsp), %r14
	movq -48(%rsp), %r15
	ret
PROC_END
