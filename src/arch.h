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

void * mm_stack_init(void (*func)(void), char *stack, size_t size)
	__attribute__((nonnull(1, 2)));

void mm_stack_switch(void **old_sp, void **new_sp)
	__attribute__((nonnull(1, 2)));

#endif /* ARCH_H */
