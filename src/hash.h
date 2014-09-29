/*
 * hash.h - MainMemory hash functions.
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

#ifndef HASH_H
#define HASH_H

#include "common.h"
#include "bitops.h"

/**********************************************************************
 *  D. J. Bernstein's hash function.
 **********************************************************************/

static inline uint32_t
mm_hash_djb(const void *data, size_t size)
{
	uint32_t h = 0;

	const uint8_t *p = (uint8_t *) data;
	const uint8_t *e = (uint8_t *) data + size;
	while (p < e) {
		h = 33 * h ^ *p++;
	}

	return h;
}

/**********************************************************************
 * The Fowler/Noll/Vo (FNV) hash function, variant 1a.
 **********************************************************************/

/*
 * http://www.isthe.com/chongo/tech/comp/fnv/index.html
 */

#define MM_HASH_FNV1_32_INIT ((uint32_t) 0x811c9dc5)
#define MM_HASH_FNV_32_PRIME ((uint32_t) 0x01000193)

static inline uint32_t
mm_hash_fnv(const void *data, size_t size)
{
	uint32_t h = MM_HASH_FNV1_32_INIT;

	const uint8_t *p = (uint8_t *) data;
	const uint8_t *e = (uint8_t *) data + size;
	while (p < e) {
		h ^= (uint32_t) *p++;
		h *= MM_HASH_FNV_32_PRIME;
	}

	return h;
}

/**********************************************************************
 * MurmurHash3 32-bit function.
 **********************************************************************/

uint32_t mm_hash_murmur3_32(const void *data, size_t size);

#endif /* HASH_H */
