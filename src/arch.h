/*
 * arch.h - MainMemory architecture-specific layer.
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

#ifndef ARCH_H
#define ARCH_H

#include "common.h"

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
