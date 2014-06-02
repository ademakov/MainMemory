/*
 * hash.c - MainMemory hash functions.
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
#include "bits.h"

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

#define MM_HASH_MURMUR_C1 ((uint32_t) 0xcc9e2d51)
#define MM_HASH_MURMUR_C2 ((uint32_t) 0x1b873593)

static inline uint32_t
mm_hash_murmur3_32(const void *data, size_t size)
{
	uint32_t h = 0;

	const uint32_t *b = (const uint32_t *) data;
	const uint32_t *e = b + size / 4;
	while (b < e) {
		uint32_t k = *b++;

		k *= MM_HASH_MURMUR_C1;
		k = mm_rotl32(k, 15);
		k *= MM_HASH_MURMUR_C2;

		h ^= k;
		h = mm_rotl32(h, 13);
		h = h * 5 + 0xe6546b64;
	}

	const uint8_t *t = (uint8_t *) e;
	uint32_t k = 0;
	switch(size & 3) {
	case 3:
		k ^= t[2] << 16;
	case 2:
		k ^= t[1] << 8;
	case 1:
		k ^= t[0];
	};

	k *= MM_HASH_MURMUR_C1;
	k = mm_rotl32(k, 15);
	k *= MM_HASH_MURMUR_C2;

	h ^= k;
	h ^= size;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

#endif /* HASH_H */
