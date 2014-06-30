/*
 * arch/spin.h - MainMemory spinning pause.
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

#ifndef ARCH_SPIN_H
#define ARCH_SPIN_H

#include "config.h"

#if ARCH_X86
# include "arch/x86/spin.h"
#elif ARCH_X86_64
# include "arch/x86-64/spin.h"
#else
# include "arch/generic/spin.h"
#endif

#endif /* ARCH_SPIN_H */
