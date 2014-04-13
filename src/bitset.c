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

#include "bits.h"

void
mm_bitset_prepare(struct mm_bitset *set, const struct mm_allocator *alloc,
		  size_t size)
{
	set->size = size;
	if (mm_bitset_is_small(set)) {
		set->small_set = 0;
	} else {
		size_t words = (size + MM_BITSET_UNIT - 1) / MM_BITSET_UNIT;
		set->large_set = (alloc->calloc)(words, sizeof(uintptr_t));
	}
}

void
mm_bitset_cleanup(struct mm_bitset *set, const struct mm_allocator *alloc)
{
	if (mm_bitset_is_small(set)) {
		// Nothing to do.
	} else {
		(alloc->free)(set->large_set);
	}
}

size_t
mm_bitset_count(const struct mm_bitset *set)
{
	if (mm_bitset_is_small(set)) {
		return mm_popcount(set->small_set);
	} else {
		size_t count = 0;
		size_t words = (set->size + MM_BITSET_UNIT - 1) / MM_BITSET_UNIT;
		for (size_t i = 0; i < words; i++) {
			count += mm_popcount(set->large_set[i]);
		}
		return count;
	}
}

void
mm_bitset_set_all(struct mm_bitset *set)
{
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = ((uintptr_t) -1);
		if (MM_BITSET_UNIT > set->size)
			mask >>= (MM_BITSET_UNIT - set->size);
		set->small_set = mask;
	} else {
		size_t words = set->size / MM_BITSET_UNIT;
		size_t bits = set->size % MM_BITSET_UNIT;
		for (size_t i = 0; i < words; i++) {
			set->large_set[i] = ((uintptr_t) -1);
		}
		if (bits) {
			uintptr_t mask = ((uintptr_t) -1);
			mask >>= (MM_BITSET_UNIT - bits);
			set->large_set[words] = mask;
		}
	}
}

void
mm_bitset_flip_all(struct mm_bitset *set)
{
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = ((uintptr_t) -1);
		if (MM_BITSET_UNIT > set->size)
			mask >>= (MM_BITSET_UNIT - set->size);
		set->small_set ^= mask;
	} else {
		size_t words = set->size / MM_BITSET_UNIT;
		size_t bits = set->size % MM_BITSET_UNIT;
		for (size_t i = 0; i < words; i++) {
			set->large_set[i] ^= ((uintptr_t) -1);
		}
		if (bits) {
			uintptr_t mask = ((uintptr_t) -1);
			mask >>= (MM_BITSET_UNIT - bits);
			set->large_set[words] ^= mask;
		}
	}
}

void
mm_bitset_clear_all(struct mm_bitset *set)
{
	if (mm_bitset_is_small(set)) {
		set->small_set = 0;
	} else {
		size_t words = (set->size + MM_BITSET_UNIT - 1) / MM_BITSET_UNIT;
		for (size_t i = 0; i < words; i++) {
			set->large_set[i] = 0;
		}
	}
}

bool
mm_bitset_any(const struct mm_bitset *set)
{
	if (mm_bitset_is_small(set)) {
		return set->small_set != 0;
	} else {
		size_t words = (set->size + MM_BITSET_UNIT - 1) / MM_BITSET_UNIT;
		for (size_t i = 0; i < words; i++) {
			if (set->large_set[i] != 0)
				return true;
		}
		return false;
	}
}

bool
mm_bitset_all(const struct mm_bitset *set)
{
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = ((uintptr_t) -1);
		if (MM_BITSET_UNIT > set->size)
			mask >>= (MM_BITSET_UNIT - set->size);
		return set->small_set == mask;
	} else {
		size_t words = set->size / MM_BITSET_UNIT;
		size_t bits = set->size % MM_BITSET_UNIT;
		for (size_t i = 0; i < words; i++) {
			if (set->large_set[i] != ((uintptr_t) -1))
				return false;
		}
		if (bits) {
			uintptr_t mask = ((uintptr_t) -1);
			mask >>= (MM_BITSET_UNIT - bits);
			return set->large_set[words] == mask;
		}
		return true;
	}
}

void
mm_bitset_or(struct mm_bitset *set, const struct mm_bitset *set2)
{
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = ((uintptr_t) -1);
		if (MM_BITSET_UNIT > set->size)
			mask >>= (MM_BITSET_UNIT - set->size);
		if (mm_bitset_is_small(set2)) {
			set->small_set |= (set2->small_set & mask);
		} else {
			set->small_set |= (set2->large_set[0] & mask);
		}
	} else {
		if (mm_bitset_is_small(set2)) {
			set->large_set[0] |= set2->small_set;
		} else {
			size_t size = min(set->size, set2->size);
			size_t words = size / MM_BITSET_UNIT;
			size_t bits = size % MM_BITSET_UNIT;
			for (size_t i = 0; i < words; i++) {
				set->large_set[i] |= set2->large_set[i];
			}
			if (bits) {
				uintptr_t mask = ((uintptr_t) -1);
				mask >>= (MM_BITSET_UNIT - bits);
				set->large_set[words] |= (set2->large_set[words] & mask);
			}
		}
	}
}

void
mm_bitset_and(struct mm_bitset *set, const struct mm_bitset *set2)
{
	if (mm_bitset_is_small(set)) {
		if (mm_bitset_is_small(set2)) {
			set->small_set &= set2->small_set;
		} else {
			set->small_set &= set2->large_set[0];
		}
	} else {
		size_t erase;
		if (mm_bitset_is_small(set2)) {
			set->large_set[0] &= set2->small_set;
			erase = 1;
		} else {
			size_t size = min(set->size, set2->size);
			size_t words = size / MM_BITSET_UNIT;
			size_t bits = size % MM_BITSET_UNIT;
			for (size_t i = 0; i < words; i++) {
				set->large_set[i] &= set2->large_set[i];
			}
			erase = words;
			if (bits) {
				erase++;
				uintptr_t mask = ((uintptr_t) -1);
				mask >>= (MM_BITSET_UNIT - bits);
				set->large_set[words] &= (set2->large_set[words] & mask);
			}
		}

		size_t erase_end = (set->size + MM_BITSET_UNIT - 1) / MM_BITSET_UNIT;
		for (; erase < erase_end; erase++) {
			set->large_set[erase] = 0;
		}
	}
}
