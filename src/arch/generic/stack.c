/*
 * stack.c - MainMemory arch-specific stack support.
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
#include "util.h"

void
mm_stack_init(mm_stack_ctx_t *ctx,
	      void (*func)(void),
	      char *stack, size_t size)
{
	if (unlikely(getcontext(ctx) < 0))
		mm_fatal(errno, "getcontext");
	ctx->uc_link = NULL;
	ctx->uc_stack.ss_sp = stack;
	ctx->uc_stack.ss_size = size;
	makecontext(ctx, func, 0);
}

void
mm_stack_switch(mm_stack_ctx_t *old_ctx,
		mm_stack_ctx_t *new_ctx)
{
	swapcontext(old_ctx, new_ctx);
}
