/*
 * table.h - MainMemory memcache entry table.
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

#ifndef MEMCACHE_TABLE_H
#define MEMCACHE_TABLE_H

#include "memcache.h"
#include "entry.h"

#include "../bits.h"
#include "../list.h"
#include "../lock.h"

/* A partition of table of memcache entries. */
struct mc_tpart
{
	/* The hash table buckets. */
	struct mm_link *buckets;
	/* The LRU list of entries. */
	struct mm_list evict_list;

	/* The number of buckets. */
	uint32_t nbuckets;
	/* The number of used entries. */
	uint32_t nentries;
	/* The total data size in all entries. */
	size_t nbytes;

#if ENABLE_MEMCACHE_LOCKS
	mm_task_lock_t lock;
#else
	mm_core_t core;
#endif

	bool evicting;
	bool striding;

	/* The last used value for CAS command. */
	uint64_t cas;

} __align(MM_CACHELINE);

/* The table of memcache entries. */
struct mc_table
{
	/* Table partitions. */
	struct mc_tpart *parts;
	/* The number of table partitions. */
	mm_core_t nparts;
	/* The hash value bits that identify partition. */
	uint16_t part_bits;
	uint32_t part_mask;
	/* The data size per partition that causes data eviction. */
	uint32_t nbytes_threshold;
	/* The maximum number of buckets per partition. */
	size_t nbuckets_max;
	/* Base table address. */
	void *address;
};

extern struct mc_table mc_table;

/**********************************************************************
 * Memcache table initialization and termination.
 **********************************************************************/

void mc_table_init(const struct mm_memcache_config *config)
	__attribute__((nonnull(1)));

void mc_table_term(void);

/**********************************************************************
 * Memcache table access routines.
 **********************************************************************/

static inline struct mc_tpart *
mc_table_part(uint32_t hash)
{
	return &mc_table.parts[hash & mc_table.part_mask];
}

static inline void
mc_table_lock(struct mc_tpart *part)
{
#if ENABLE_MEMCACHE_LOCKS
	mm_task_lock(&part->lock);
#else
	(void) part;
#endif
}

static inline void
mc_table_unlock(struct mc_tpart *part)
{
#if ENABLE_MEMCACHE_LOCKS
	mm_task_unlock(&part->lock);
#else
	(void) part;
#endif
}

struct mc_entry * mc_table_lookup(struct mc_tpart *part, uint32_t hash, const char *key, uint8_t key_len)
	__attribute__((nonnull(1)));

struct mc_entry * mc_table_remove(struct mc_tpart *part, uint32_t hash, const char *key, uint8_t key_len)
	__attribute__((nonnull(1)));

void mc_table_insert(struct mc_tpart *part, uint32_t hash, struct mc_entry *entry)
	__attribute__((nonnull(1)));

static inline void
mc_table_touch(struct mc_tpart *part, struct mc_entry *entry)
{
	// Maintain the LRU order.
	mm_list_delete(&entry->evict_list);
	mm_list_append(&part->evict_list, &entry->evict_list);
}

bool mc_table_evict(struct mc_tpart *part, size_t nrequired);

#endif /* MEMCACHE_TABLE_H */
