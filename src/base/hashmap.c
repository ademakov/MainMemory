/*
 * base/hashmap.c - Basic hash table.
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

#include "base/hashmap.h"

#include "base/log/debug.h"

static uint32_t mm_hashmap_nbuckets[] = {
	29u, 43u, 61u, 97u, 139u,
	211u, 317u, 491u, 743u, 1109u,

	1669u, 2503u, 3761u, 5641u, 8461u,
	12697u, 19037u, 28571u, 42853u, 64283u,

	96431u, 144629u, 216973u, 325459u, 488171u,
	732283u, 1098401u, 1647617u, 2471449u, 3707167u,

	5560771u, 8341153u, 12511721u, 18767629u, 28151447u,
	42227173u, 63340751u, 95011151u, 142516723u, 213775043u,

	320662637u, 480993899u, 721490923u, 1082236387u, 1623354563u,
	2435031871u, 3652547849u,
};

static size_t mm_hashmap_nnbuckets = sizeof(mm_hashmap_nbuckets) / sizeof(mm_hashmap_nbuckets[0]);

static ssize_t
mm_hashmap_nbuckets_index(uint32_t nbuckets)
{
	for (size_t i = 0; i < mm_hashmap_nnbuckets; i++)
		if (mm_hashmap_nbuckets[i] == nbuckets)
			return i;
	return -1;
}

static struct mm_stack *
mm_hashmap_alloc(struct mm_hashmap *map, uint32_t nbuckets)
{
	return mm_arena_calloc(map->arena, sizeof(struct mm_stack), nbuckets);
}

static void
mm_hashmap_rehash(struct mm_hashmap *map, uint32_t nbuckets)
{
	struct mm_stack *buckets = mm_hashmap_alloc(map, nbuckets);
	for (uint32_t i = 0; i < map->nbuckets; i++) {
		struct mm_stack *src = &map->buckets[i];
		while (!mm_stack_empty(src)) {
			struct mm_slink *link = mm_stack_remove(src);
			struct mm_hashmap_entry *entry
				= containerof(link, struct mm_hashmap_entry, link);
			struct mm_stack *dst = &buckets[entry->hash % nbuckets];
			mm_stack_insert(dst, &entry->link);
		}
	}

	mm_arena_free(map->arena, map->buckets);
	map->buckets = buckets;
	map->nbuckets = nbuckets;
}

void NONNULL(1, 2)
mm_hashmap_prepare(struct mm_hashmap *map, mm_arena_t arena)
{
	uint32_t nbuckets = mm_hashmap_nbuckets[0];

	map->arena = arena;
	map->buckets = mm_hashmap_alloc(map, nbuckets);
	map->nbuckets = nbuckets;
	map->nentries = 0;
}

void NONNULL(1, 2)
mm_hashmap_cleanup(struct mm_hashmap *map, mm_hashmap_free_entry_t free_entry)
{
	for (uint32_t i = 0; i < map->nbuckets; i++) {
		struct mm_stack *src = &map->buckets[i];
		while (!mm_stack_empty(src)) {
			struct mm_slink *link = mm_stack_remove(src);
			struct mm_hashmap_entry *entry
				= containerof(link, struct mm_hashmap_entry, link);
			(*free_entry)(map, entry);
		}
	}

	mm_arena_free(map->arena, map->buckets);
}

struct mm_hashmap_entry * NONNULL(1, 2)
mm_hashmap_lookup(struct mm_hashmap *map, const char *key, size_t keylen)
{
	if (unlikely(keylen > UINT32_MAX))
		return NULL;

	uint32_t hash = mm_hashmap_hash(key, keylen);
	struct mm_stack *bucket = &map->buckets[hash % map->nbuckets];

	struct mm_slink *link = mm_stack_head(bucket);
	while (link != NULL) {
		struct mm_hashmap_entry *entry = containerof(link, struct mm_hashmap_entry, link);
		if (entry->hash == hash
		    && entry->keylen == keylen
		    && memcmp(entry->key, key, keylen) == 0)
			return entry;
		link = link->next;
	}

	return NULL;
}

void NONNULL(1, 2)
mm_hashmap_insert(struct mm_hashmap *map, struct mm_hashmap_entry *entry)
{
	struct mm_stack *bucket = &map->buckets[entry->hash % map->nbuckets];

	mm_stack_insert(bucket, &entry->link);

	if (++map->nentries > map->nbuckets * 3) {
		ssize_t nbi = mm_hashmap_nbuckets_index(map->nbuckets);
		if (nbi < 0)
			ABORT();
		if ((size_t) (nbi + 1) < mm_hashmap_nnbuckets)
			mm_hashmap_rehash(map, mm_hashmap_nbuckets[nbi + 1]);
	}
}

void NONNULL(1, 2)
mm_hashmap_remove(struct mm_hashmap *map, struct mm_hashmap_entry *entry)
{
	struct mm_stack *bucket = &map->buckets[entry->hash % map->nbuckets];

	struct mm_slink *prev = &bucket->head;
	for (;;) {
		if (unlikely(prev->next == NULL))
			ABORT();
		if (prev->next == &entry->link) {
			mm_stack_remove_next(prev);
			break;
		}
		prev = prev->next;
	}

	if (--map->nentries < map->nbuckets) {
		ssize_t nbi = mm_hashmap_nbuckets_index(map->nbuckets);
		if (nbi < 0)
			ABORT();
		if (nbi > 0)
			mm_hashmap_rehash(map, mm_hashmap_nbuckets[nbi - 1]);
	}
}
