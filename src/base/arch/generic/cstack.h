/*
 * base/arch/generic/cstack.h - MainMemory arch-specific call stack support.
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

#ifndef BASE_ARCH_GENERIC_CSTACK_H
#define BASE_ARCH_GENERIC_CSTACK_H

#if HAVE_UCONTEXT_H
#  include <ucontext.h>
#else
#  error "Unsupported architecture."
#endif

typedef ucontext_t mm_cstack_t;

static inline void
mm_cstack_switch(mm_cstack_t *old_ctx, mm_cstack_t *new_ctx)
{
	swapcontext(old_ctx, new_ctx);
}

#endif /* BASE_ARCH_GENERIC_CSTACK_H */
