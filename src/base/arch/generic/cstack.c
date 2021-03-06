/*
 * base/arch/generic/cstack.c - Arch-specific call stack support.
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

#include "base/cstack.h"

#include "base/report.h"

void
mm_cstack_prepare(mm_cstack_t *ctx, void (*entry)(void), char *stack, size_t size)
{
	if (unlikely(getcontext(ctx) < 0))
		mm_fatal(errno, "getcontext");
	ctx->uc_link = NULL;
	ctx->uc_stack.ss_sp = stack;
	ctx->uc_stack.ss_size = size;
	makecontext(ctx, entry, 0);
}
