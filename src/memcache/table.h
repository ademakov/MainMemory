/*
 * memcache/table.h - MainMemory memcache entry table.
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

#ifndef MEMCACHE_TABLE_H
#define MEMCACHE_TABLE_H

#include "memcache/memcache.h"
#include "memcache/entry.h"

#include "base/bitops.h"
#include "base/counter.h"
#include "base/list.h"
#include "base/event/event.h"
#include "base/memory/cache.h"
#include "base/thread/local.h"

#if ENABLE_MEMCACHE_LOCKING
# include "base/lock.h"
#endif

#define MC_STAT_LIST(_) 	\
	_(cmd_get)		\
	_(cmd_set)		\
	_(cmd_touch)		\
	_(cmd_flush)		\
	_(get_hits)		\
	_(get_misses)		\
	_(delete_hits)		\
	_(delete_misses)	\
	_(incr_hits)		\
	_(incr_misses)		\
	_(decr_hits)		\
	_(decr_misses)		\
	_(cas_hits)		\
	_(cas_misses)		\
	_(cas_badval)		\
	_(touch_hits)		\
	_(touch_misses)

struct mc_stat
{
#define MM_STAT_FIELD(x)	struct mm_counter x;
	MC_STAT_LIST(MM_STAT_FIELD)
#undef MM_STAT_FIELD
};

/* A partition of table of memcache entries. */
struct mc_tpart
{
	/* The hash table buckets. */
	struct mm_stack *buckets;

	/* The pool of all table entries. */
	struct mc_entry *entries;
	struct mc_entry *entries_end;

	/* Current eviction pointer. */
	struct mc_entry *clock_hand;

	/* The list of unused entries. */
	struct mm_stack free_list;

	/* The number of buckets. */
	uint32_t nbuckets;
	/* The number of entries. */
	uint32_t nentries;
	uint32_t nentries_void;
	uint32_t nentries_free;

	/* The memory space for key/value data. */
	struct mm_memory_cache data_space;

	/* The total data size of all entries. */
	size_t volume;

#if ENABLE_MEMCACHE_COMBINER
	struct mm_combiner *combiner;
#elif ENABLE_MEMCACHE_DELEGATE
	struct mm_strand *target;
#elif ENABLE_MEMCACHE_LOCKING
	mm_regular_lock_t lookup_lock;
	mm_regular_lock_t freelist_lock;
#endif

#if ENABLE_SMP
	mm_regular_lock_t evicting;
	mm_regular_lock_t striding;
#else
	bool evicting;
	bool striding;
#endif

	/* The last used value for CAS command. */
	uint64_t stamp;
	uint64_t flush_stamp;

} CACHE_ALIGN;

/* The table of memcache entries. */
struct mc_table
{
	/* Table partitions. */
	struct mc_tpart *parts;
	/* The number of table partitions. */
	mm_thread_t nparts;

	/* Current time for entry expiration checks. */
	mm_atomic_uint32_t time;

	/* The hash value bits that identify partition. */
	uint32_t part_bits;
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

	/* Entry expiration timer. */
	struct mm_event_timer exp_timer;

	/* Statistics. */
	MM_THREAD_LOCAL(struct mc_stat, stat);
};

extern struct mc_table mc_table;

/**********************************************************************
 * Memcache table initialization and termination.
 **********************************************************************/

void NONNULL(1)
mc_table_start(const struct mm_memcache_config *config);

void
mc_table_stop(void);

/**********************************************************************
 * Memcache general table routines.
 **********************************************************************/

static inline struct mc_tpart *
mc_table_part(uint32_t hash)
{
	return &mc_table.parts[hash & mc_table.part_mask];
}

static inline uint32_t NONNULL(1)
mc_table_index(struct mc_tpart *part, uint32_t hash)
{
	ASSERT(part == mc_table_part(hash));

 	uint32_t used = mm_memory_load(part->nbuckets);
	uint32_t size = mm_upper_pow2(used);
	uint32_t mask = size - 1;

	uint32_t index = (hash >> mc_table.part_bits) & mask;
	if (index >= used)
		index -= size / 2;

	return index;
}

static inline void NONNULL(1)
mc_table_lookup_lock(struct mc_tpart *part)
{
#if ENABLE_SMP && ENABLE_MEMCACHE_LOCKING
	mm_regular_lock(&part->lookup_lock);
#else
	(void) part;
#endif
}

static inline void NONNULL(1)
mc_table_lookup_unlock(struct mc_tpart *part)
{
#if ENABLE_SMP && ENABLE_MEMCACHE_LOCKING
	mm_regular_unlock(&part->lookup_lock);
#else
	(void) part;
#endif
}

static inline void NONNULL(1)
mc_table_freelist_lock(struct mc_tpart *part)
{
#if ENABLE_SMP && ENABLE_MEMCACHE_LOCKING
	mm_regular_lock(&part->freelist_lock);
#else
	(void) part;
#endif
}

static inline void NONNULL(1)
mc_table_freelist_unlock(struct mc_tpart *part)
{
#if ENABLE_SMP && ENABLE_MEMCACHE_LOCKING
	mm_regular_unlock(&part->freelist_lock);
#else
	(void) part;
#endif
}

void NONNULL(1)
mc_table_buckets_resize(struct mc_tpart *part, uint32_t old_nbuckets, uint32_t new_nbuckets);

void NONNULL(1)
mc_table_entries_resize(struct mc_tpart *part, uint32_t old_nentries, uint32_t new_nentries);

bool NONNULL(1)
mc_table_expand(struct mc_tpart *part, uint32_t n);

void NONNULL(1)
mc_table_reserve_volume(struct mc_tpart *part);

void NONNULL(1)
mc_table_reserve_entries(struct mc_tpart *part);

#endif /* MEMCACHE_TABLE_H */
