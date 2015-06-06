/*
 * arch/lock.h - MainMemory test-and-set lock primitives.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#ifndef ARCH_LOCK_H
#define ARCH_LOCK_H

#include "config.h"
#include "arch/memory.h"

/*
 * mm_lock_acquire() is a test-and-set atomic operation along with
 * acquire fence.
 *
 * mm_lock_release() is a simple clear operation along with release
 * fence.
 */

#if ARCH_X86
# include "arch/x86/lock.h"
#elif ARCH_X86_64
# include "arch/x86-64/lock.h"
#else
# include "arch/generic/lock.h"
#endif

#endif /* ARCH_LOCK_H */
