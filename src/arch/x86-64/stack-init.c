/*
 * stack_init.c - MainMemory arch-specific stack support.
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

#include "arch.h"

void
mm_stack_init(mm_stack_ctx_t *ctx,
	      void (*func)(void),
	      char *stack, size_t size)
{
	intptr_t *sp = (intptr_t *) (stack + size) - 2;

	// padding
	1[sp] = 0;
	// return address pointed by rsp
	0[sp] = (intptr_t) func;
	// callee-saved registers
	(-1)[sp] = -1L;	// rbp
	(-2)[sp] = 0;	// rbx
	(-3)[sp] = 0;	// r12
	(-4)[sp] = 0;	// r13
	(-5)[sp] = 0;	// r15
	(-6)[sp] = 0;	// r16

	*ctx = (void *) sp;
}
