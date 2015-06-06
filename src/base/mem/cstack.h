/*
 * base/mem/cstack.h - MainMemory call stack support.
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

#ifndef BASE_MEM_CSTACK_H
#define BASE_MEM_CSTACK_H

#include "common.h"

void * mm_cstack_create(uint32_t stack_size, uint32_t guard_size);

void mm_cstack_destroy(void *stack, uint32_t stack_size);

#endif /* BASE_MEM_CSTACK_H */
