/*
 * memcache/table.h - MainMemory memcache entry table.
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

#include "memcache/memcache.h"
#include "memcache/action.h"
#include "memcache/entry.h"

#include "base/bitops.h"
#include "base/list.h"
#include "base/lock.h"
#include "core/wait.h"

/* A partition of table of memcache entries. */
struct mc_tpart
{
	/* The hash table buckets. */
	struct mm_link *buckets;

	/* The pool of all table entries. */
	struct mc_entry *entries;
	struct mc_entry *entries_end;

	/* Current eviction pointer. */
	struct mc_entry *clock_hand;

	/* The list of unused entries. */
	struct mm_link free_list;

	/* The number of buckets. */
	uint32_t nbuckets;
	/* The number of entries. */
	uint32_t nentries;
	uint32_t nentries_void;
	uint32_t nentries_free;

	/* The total data size of all entries. */
	size_t volume;

	struct mm_waitset waitset;

#if ENABLE_MEMCACHE_LOCKS
	mm_task_lock_t lock;
#else
	mm_core_t core;
#endif

	bool evicting;
	bool striding;

	/* The last used value for CAS command. */
	uint64_t cas;

} __align_cacheline;

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

	/* The maximum number of buckets per partition. */
	uint32_t nbuckets_max;
	/* The maximum number of entries per partition. */
	uint32_t nentries_max;
	/* The number of entries added on expansion. */
	uint32_t nentries_increment;
	/* The data size per partition that causes data eviction. */
	size_t volume_max;

	/* Base table addresses. */
	void *buckets_base;
	void *entries_base;
};

extern struct mc_table mc_table;

/**********************************************************************
 * Memcache table initialization and termination.
 **********************************************************************/

void mc_table_init(const struct mm_memcache_config *config)
	__attribute__((nonnull(1)));

void mc_table_term(void);

/**********************************************************************
 * Memcache general table routines.
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

/**********************************************************************
 * Table entry creation and destruction routines.
 **********************************************************************/

bool mc_table_evict(struct mc_tpart *part, uint32_t nrequired)
	__attribute__((nonnull(1)));

struct mc_entry * mc_table_create_entry(struct mc_tpart *part)
	__attribute__((nonnull(1)));

void mc_table_destroy_entry(struct mc_tpart *part, struct mc_entry *entry)
	__attribute__((nonnull(1, 2)));

static inline void
mc_table_ref_entry(struct mc_entry *entry)
{
	uint32_t test;
#if ENABLE_SMP
	test = mm_atomic_uint16_inc_and_test(&entry->ref_count);
#else
	test = ++(entry->ref_count);
#endif
	if (unlikely(!test))
		ABORT();
}

static inline void
mc_table_unref_entry(struct mc_tpart *part, struct mc_entry *entry)
{
	uint32_t test;
#if ENABLE_SMP
	test = mm_atomic_uint16_dec_and_test(&entry->ref_count);
#else
	test = --(entry->ref_count);
#endif
	if (!test)
		mc_table_destroy_entry(part, entry);
}

/**********************************************************************
 * Table entry access routines.
 **********************************************************************/

void mc_table_lookup(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_table_remove(struct mc_action *action)
	__attribute__((nonnull(1)));

void mc_table_insert(struct mc_action *action, struct mc_entry *entry, uint8_t state)
	__attribute__((nonnull(1)));

#endif /* MEMCACHE_TABLE_H */
