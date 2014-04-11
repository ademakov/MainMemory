/*
 * bitset.h - MainMemory bit sets.
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

#include "bitset.h"

void
mm_bitset_prepare(struct mm_bitset *bitset, struct mm_allocator *alloc, size_t size)
{
	bitset->size = size;
	if (mm_bitset_is_small(bitset)) {
		bitset->small_set = 0;
	} else {
		size_t words = (size + MM_BITSET_UNIT - 1) / MM_BITSET_UNIT;
		bitset->large_set = (alloc->calloc)(words, sizeof(uintptr_t));
	}
}

void
mm_bitset_cleanup(struct mm_bitset *bitset, struct mm_allocator *alloc)
{
	if (mm_bitset_is_small(bitset)) {
		// Nothing to do.
	} else {
		(alloc->free)(bitset->large_set);
	}
}
