/*
 * base/arch/x86-64/cstack.c - Arch-specific call stack support.
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

#include "base/cstack.h"
#include "base/report.h"

#include <stdint.h>

static void
mm_cstack_abort(void)
{
	ABORT();
}

void
mm_cstack_prepare(mm_cstack_t *ctx, void (*entry)(void), char *stack, size_t size)
{
	uintptr_t *sp = (uintptr_t *) (stack + size);

	// The top of the stack must contain an address of the caller.
	// And it must be 8 bytes off from a 16-byte aligned address.
	0[--sp] = (uintptr_t) mm_cstack_abort;

	ctx->store[0] = (uintptr_t) sp;		// rsp
	ctx->store[1] = (uintptr_t) sp;		// rbp
	ctx->store[2] = (uintptr_t) entry;	// jmp address
}
