/*
 * base/memory/alloc.h - MainMemory memory allocation.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#ifndef BASE_MEMORY_ALLOC_H
#define BASE_MEMORY_ALLOC_H

/* DLMalloc alignment. */
#define MM_ALLOC_ALIGNMENT	(8)
#define MM_ALLOC_ALIGNMENT_BITS	(3)

/* DLMalloc overhead. */
#if MM_WORD_32BIT
# ifndef FOOTERS
#  define MM_ALLOC_OVERHEAD	(4)
# else
#  define MM_ALLOC_OVERHEAD	(8)
# endif
#else
# ifndef FOOTERS
#  define MM_ALLOC_OVERHEAD	(8)
# else
#  define MM_ALLOC_OVERHEAD	(16)
# endif
#endif

#endif /* BASE_MEMORY_ALLOC_H */
