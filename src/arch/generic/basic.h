/*
 * arch/generic/basic.h - MainMemory basic architecture properties.
 *
 * Copyright (C) 2013  Aleksey Demakov
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

#ifndef ARCH_GENERIC_BASIC_H
#define ARCH_GENERIC_BASIC_H

/* General purpose register width. */
#if !defined(MM_WORD_32BIT) && !defined(MM_WORD_64BIT)
# if __LP64__
#  define MM_WORD_64BIT		1
# else
#  define MM_WORD_32BIT		1
# endif
#endif

/* Cache line size. */
#define MM_CACHELINE		(64)

/* Virtual memory page size. */
#define MM_PAGE_SIZE		(4096)

#endif /* ARCH_GENERIC_BASIC_H */
