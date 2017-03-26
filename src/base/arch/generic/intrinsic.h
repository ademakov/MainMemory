/*
 * base/arch/generic/intrinsic.h - Architecture-specific intrinsics.
 *
 * Copyright (C) 2013-2017  Aleksey Demakov
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

#ifndef BASE_ARCH_GENERIC_INTRINSIC_H
#define BASE_ARCH_GENERIC_INTRINSIC_H

/**********************************************************************
 * Architecture-specific back off primitive.
 **********************************************************************/

static inline void
mm_cpu_backoff(void)
{
	for (int i = 0; i < 64; i++)
		mm_compiler_barrier();
}

#endif /* BASE_ARCH_GENERIC_INTRINSIC_H */
