/*
 * base/cstack.h - MainMemory arch-specific call stack support.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#ifndef BASE_CSTACK_H
#define BASE_CSTACK_H

#include "common.h"

#include <stddef.h>

/**********************************************************************
 * Architecture-specific call stack definitions.
 **********************************************************************/

#if ARCH_X86
# include "base/arch/x86/cstack.h"
#elif ARCH_X86_64
# include "base/arch/x86-64/cstack.h"
#else
# include "base/arch/generic/cstack.h"
#endif

/**********************************************************************
 * Common call stack routines.
 **********************************************************************/

void *
mm_cstack_create(uint32_t stack_size, uint32_t guard_size);

void NONNULL(1)
mm_cstack_destroy(void *stack, uint32_t stack_size);

void NONNULL(1, 2)
mm_cstack_prepare(mm_cstack_t *ctx, void (*entry)(void), char *stack, size_t size);

void NONNULL(1, 2)
mm_cstack_switch(mm_cstack_t *old_ctx, mm_cstack_t *new_ctx);

#endif /* BASE_CSTACK_H */
