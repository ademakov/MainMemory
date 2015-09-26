/*
 * base/bitset.h - MainMemory bit sets.
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

#ifndef BASE_BITSET_H
#define BASE_BITSET_H

#include "common.h"
#include "base/log/debug.h"
#include "base/memory/arena.h"

#define MM_BITSET_UNIT		(sizeof(uintptr_t) * 8)

#define MM_BITSET_NONE		((size_t) -1)

struct mm_bitset
{
	size_t size;
	union
	{
		uintptr_t small_set;
		uintptr_t *large_set;
	};
};

void mm_bitset_prepare(struct mm_bitset *set, mm_arena_t arena, size_t size)
	__attribute__((nonnull(1)));

void mm_bitset_cleanup(struct mm_bitset *set, mm_arena_t arena)
	__attribute__((nonnull(1)));

static inline bool
mm_bitset_is_small(const struct mm_bitset *set)
{
	return set->size <= MM_BITSET_UNIT;
}

static inline size_t
mm_bitset_size(const struct mm_bitset *set)
{
	return set->size;
}

static inline bool
mm_bitset_test(const struct mm_bitset *set, size_t bit)
{
	ASSERT(bit < set->size);
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = (uintptr_t) 1 << bit;
		return (set->small_set & mask) != 0;
	} else {
		size_t word = bit / MM_BITSET_UNIT;
		uintptr_t mask = (uintptr_t) 1 << (bit % MM_BITSET_UNIT);
		return (set->large_set[word] & mask) != 0;
	}
}

static inline void
mm_bitset_set(struct mm_bitset *set, size_t bit)
{
	ASSERT(bit < set->size);
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = (uintptr_t) 1 << bit;
		set->small_set |= mask;
	} else {
		size_t word = bit / MM_BITSET_UNIT;
		uintptr_t mask = (uintptr_t) 1 << (bit % MM_BITSET_UNIT);
		set->large_set[word] |= mask;
	}
}

static inline void
mm_bitset_flip(struct mm_bitset *set, size_t bit)
{
	ASSERT(bit < set->size);
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = (uintptr_t) 1 << bit;
		set->small_set ^= mask;
	} else {
		size_t word = bit / MM_BITSET_UNIT;
		uintptr_t mask = (uintptr_t) 1 << (bit % MM_BITSET_UNIT);
		set->large_set[word] ^= mask;
	}
}

static inline void
mm_bitset_clear(struct mm_bitset *set, size_t bit)
{
	ASSERT(bit < set->size);
	if (mm_bitset_is_small(set)) {
		uintptr_t mask = (uintptr_t) 1 << bit;
		set->small_set &= ~mask;
	} else {
		size_t word = bit / MM_BITSET_UNIT;
		uintptr_t mask = (uintptr_t) 1 << (bit % MM_BITSET_UNIT);
		set->large_set[word] &= ~mask;
	}
}

bool mm_bitset_any(const struct mm_bitset *set)
	__attribute__((nonnull(1)));

bool mm_bitset_all(const struct mm_bitset *set)
	__attribute__((nonnull(1)));

size_t mm_bitset_find(const struct mm_bitset *set, size_t bit)
	__attribute__((nonnull(1)));

size_t mm_bitset_count(const struct mm_bitset *set)
	__attribute__((nonnull(1)));

void mm_bitset_set_all(struct mm_bitset *set)
	__attribute__((nonnull(1)));

void mm_bitset_flip_all(struct mm_bitset *set)
	__attribute__((nonnull(1)));

void mm_bitset_clear_all(struct mm_bitset *set)
	__attribute__((nonnull(1)));

void mm_bitset_or(struct mm_bitset *set, const struct mm_bitset *set2)
	__attribute__((nonnull(1, 2)));

void mm_bitset_and(struct mm_bitset *set, const struct mm_bitset *set2)
	__attribute__((nonnull(1, 2)));

#endif /* BASE_BITSET_H */
