/*
 * base/hashmap.h - Basic hash table.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#ifndef BASE_HASHMAP_H
#define BASE_HASHMAP_H

#include "common.h"
#include "base/hash.h"
#include "base/list.h"
#include "base/log/error.h"
#include "base/memory/arena.h"

/* Forward declarations. */
struct mm_hashmap;
struct mm_hashmap_entry;

#define mm_hashmap_hash mm_hash_fnv

struct mm_hashmap_entry
{
	struct mm_slink link;
	uint32_t hash;
	uint32_t keylen;
	const char *key;
};

struct mm_hashmap
{
	struct mm_stack *buckets;
	size_t nbuckets;
	size_t nentries;
	mm_arena_t arena;
};

typedef void (*mm_hashmap_free_entry_t)(struct mm_hashmap *, struct mm_hashmap_entry *);

void NONNULL(1, 2)
mm_hashmap_prepare(struct mm_hashmap *map, mm_arena_t arena);

void NONNULL(1, 2)
mm_hashmap_cleanup(struct mm_hashmap *map, mm_hashmap_free_entry_t free_entry);

struct mm_hashmap_entry * NONNULL(1, 2)
mm_hashmap_lookup(struct mm_hashmap *map, const char *key, size_t keylen);

void NONNULL(1, 2)
mm_hashmap_insert(struct mm_hashmap *map, struct mm_hashmap_entry *entry);

void NONNULL(1, 2)
mm_hashmap_remove(struct mm_hashmap *map, struct mm_hashmap_entry *entry);

static inline void NONNULL(1, 2)
mm_hashmap_setkey(struct mm_hashmap_entry *entry, const char *key, size_t keylen)
{
	if (unlikely(keylen > UINT32_MAX))
		mm_fatal(0, "too long name");

	entry->key = key;
	entry->keylen = keylen;
	entry->hash = mm_hashmap_hash(key, keylen);
}

#endif /* BASE_HASHMAP_H */
