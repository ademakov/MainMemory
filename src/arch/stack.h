/*
 * arch.h - MainMemory arch-specific stack support.
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

#ifndef ARCH_H
#define ARCH_H

#include "config.h"

#include <stddef.h>

#if ARCH_GENERIC
# if HAVE_UCONTEXT_H
#  include <ucontext.h>
# else
#  error "Unsupported architecture."
# endif
#endif

#if ARCH_GENERIC
typedef ucontext_t mm_stack_ctx_t;
#else
typedef void * mm_stack_ctx_t;
#endif

void mm_stack_init(mm_stack_ctx_t *ctx,
		   void (*func)(void),
		   char *stack, size_t size)
	__attribute__((nonnull(1, 2)));

void mm_stack_switch(mm_stack_ctx_t *old_ctx,
		     mm_stack_ctx_t *new_ctx)
	__attribute__((nonnull(1, 2)));

#endif /* ARCH_H */
