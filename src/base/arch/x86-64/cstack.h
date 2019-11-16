/*
 * base/arch/x86-64/cstack.h - MainMemory arch-specific call stack support.
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

#ifndef BASE_ARCH_X86_64_CSTACK_H
#define BASE_ARCH_X86_64_CSTACK_H

typedef struct
{
	// Space for the RSP, RBP registers and for the jump address.
	intptr_t store[3];
} mm_cstack_t;

static inline void NONNULL(1, 2)
mm_cstack_switch(mm_cstack_t *old_ctx, mm_cstack_t *new_ctx)
{
	// Let the C compiler save all the callee-saved registers for us.
	// This is done by having them in the clobber list of the inline
	// asm statement. However the rbp register cannot be used there
	// with some compiler options (-fno-omit-frame-pointer or -fpic).
	// So handle it manually. Still with other options it might be
	// assigned to one of the input values. So restore its previous
	// value only at the very end.
	asm volatile(
		"leaq 0f(%%rip), %%r12\n\t"
		"movq %%rsp, 0(%0)\n\t"
		"movq 0(%1), %%rsp\n\t"
		"movq %%r12, 16(%0)\n\t"
		"movq 16(%1), %%r12\n\t"
		"movq %%rbp, 8(%0)\n\t"
		"movq 8(%1), %%rbp\n\t"
		"jmpq *%%r12\n"
		"0:"
		:
		: "r"(old_ctx), "r"(new_ctx)
		: "cc", "memory", /*"rbp",*/ "rbx", "r12", "r13", "r14", "r15");
}

#endif /* BASE_ARCH_X86_64_CSTACK_H */
