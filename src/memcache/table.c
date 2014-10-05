/*
 * memcache/table.c - MainMemory memcache entry table.
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

#include "memcache/table.h"
#include "memcache/entry.h"

#include "hash.h"
#include "log.h"
#include "task.h"

#include <sys/mman.h>

#define MC_TABLE_STRIDE		64

#if MM_WORD_32BIT
# define MC_TABLE_SIZE_MAX	((size_t) 64 * 1024 * 1024)
#else
# define MC_TABLE_SIZE_MAX	((size_t) 512 * 1024 * 1024)
#endif

#define MC_TABLE_VOLUME_RESERVE	(64 * 1024)

struct mc_table mc_table;

/**********************************************************************
 * Helper routines.
 **********************************************************************/

static inline size_t
mc_table_buckets_size(uint16_t nparts, uint32_t nbuckets)
{
	size_t space = nbuckets * sizeof(struct mm_link);
	return nparts * mm_round_up(space, MM_PAGE_SIZE);
}

static inline size_t
mc_table_entries_size(uint16_t nparts, uint32_t nentries)
{
	size_t space = nentries * sizeof(struct mc_entry);
	return nparts * mm_round_up(space, MM_PAGE_SIZE);
}

static inline uint32_t
mc_table_index(struct mc_tpart *part, uint32_t hash)
{
	ASSERT(part == &mc_table.parts[hash & mc_table.part_mask]);

 	uint32_t used = mm_memory_load(part->nbuckets);
	uint32_t size = 1 << (32 - mm_clz(used - 1));
	uint32_t mask = size - 1;

	uint32_t index = (hash >> mc_table.part_bits) & mask;
	if (index >= used)
		index -= size / 2;

	return index;
}

static inline bool
mc_table_check_size(struct mc_tpart *part)
{
	uint32_t nb = mm_memory_load(part->nbuckets);
	uint32_t ne = mm_memory_load(part->nentries);
	ne -= mm_memory_load(part->nentries_free);
	ne -= mm_memory_load(part->nentries_void);
	return ne > (nb * 2) && nb < mc_table.nbuckets_max;
}

static inline bool
mc_table_check_volume(struct mc_tpart *part, size_t reserve)
{
	uint32_t n = mm_memory_load(part->volume);
	return (n + reserve) > mc_table.volume_max;
}

/**********************************************************************
 * Table resize.
 **********************************************************************/

static void
mc_table_resize(void *start, size_t old_size, size_t new_size)
{
	ASSERT(((intptr_t) start % MM_PAGE_SIZE) == 0);
	ASSERT((old_size % MM_PAGE_SIZE) == 0);
	ASSERT((new_size % MM_PAGE_SIZE) == 0);
	ASSERT(old_size != new_size);

	void *addr, *map_addr;
	if (old_size > new_size) {
		size_t diff = old_size - new_size;
		addr = (char *) start + new_size;
		map_addr = mmap(addr, diff, PROT_NONE,
				MAP_ANON | MAP_FIXED | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	} else {
		size_t diff = new_size - old_size;
		addr = (char *) start + old_size;
		map_addr = mmap(addr, diff, PROT_READ | PROT_WRITE,
				MAP_ANON | MAP_FIXED | MAP_PRIVATE, -1, 0);
	}

	if (map_addr == MAP_FAILED)
		mm_fatal(errno, "mmap");
	if (unlikely(map_addr != addr))
		mm_fatal(0, "mmap returned wrong address");
}

static void
mc_table_buckets_resize(struct mc_tpart *part,
			uint32_t old_nbuckets,
			uint32_t new_nbuckets)
{
	ENTER();
	ASSERT(mm_is_pow2z(old_nbuckets));
	ASSERT(mm_is_pow2(new_nbuckets));

	size_t old_size = mc_table_buckets_size(1, old_nbuckets);
	size_t new_size = mc_table_buckets_size(1, new_nbuckets);
	if (likely(old_size != new_size)) {
		mm_brief("memcache enabled buckets for partition #%d: %u, %lu bytes",
			 (int) (part - mc_table.parts), new_nbuckets,
			 (unsigned long) new_size);
		mc_table_resize(part->buckets, old_size, new_size);
	}

	LEAVE();
}

static void
mc_table_entries_resize(struct mc_tpart *part,
			uint32_t old_nentries,
			uint32_t new_nentries)
{
	ENTER();

	size_t old_size = mc_table_entries_size(1, old_nentries);
	size_t new_size = mc_table_entries_size(1, new_nentries);
	if (likely(old_size != new_size)) {
		mm_brief("memcache enabled entries for partition #%d: %u, %lu bytes",
			 (int) (part - mc_table.parts), new_nentries,
			 (unsigned long) new_size);
		mc_table_resize(part->entries, old_size, new_size);
	}

	LEAVE();
}

static void
mc_table_expand(struct mc_tpart *part, uint32_t n)
{
	ENTER();

	uint32_t old_nentries = part->nentries;
	uint32_t new_nentries = old_nentries + n;
	if (unlikely(new_nentries < old_nentries))
		new_nentries = UINT32_MAX;
	if (unlikely(new_nentries > mc_table.nentries_max))
		new_nentries = mc_table.nentries_max;
	n = new_nentries - old_nentries;

	mc_table_entries_resize(part, old_nentries, new_nentries);

	part->nentries_void += n;
	part->nentries += n;

	LEAVE();
}

static void
mc_table_stride(struct mc_tpart *part)
{
	ENTER();

	mc_table_lock(part);

	uint32_t used = mm_memory_load(part->nbuckets);

	uint32_t half_size;
	if (unlikely(mm_is_pow2z(used))) {
		half_size = used;
		mc_table_buckets_resize(part, used, used * 2);
	} else {
		half_size = 1 << (31 - mm_clz(used));
	}

	uint32_t target = used;
	uint32_t source = used - half_size;
	uint32_t mask = half_size + half_size - 1;

	for (uint32_t count = 0; count < MC_TABLE_STRIDE; count++) {
		struct mm_link s_entries, t_entries;
		mm_link_init(&s_entries);
		mm_link_init(&t_entries);

		struct mm_link *link = mm_link_head(&part->buckets[source]);
		while (link != NULL) {
			struct mm_link *next = link->next;

			struct mc_entry *entry = containerof(link, struct mc_entry, link);
			uint32_t index = (entry->hash >> mc_table.part_bits) & mask;
			if (index == source) {
				mm_link_insert(&s_entries, link);
			} else {
				ASSERT(index == target);
				mm_link_insert(&t_entries, link);
			}

			link = next;
		}

		part->buckets[source++] = s_entries;
		part->buckets[target++] = t_entries;
	}

	used += MC_TABLE_STRIDE;
	mm_memory_store(part->nbuckets, used);

	mc_table_unlock(part);

	LEAVE();
}

static mm_value_t
mc_table_stride_routine(mm_value_t arg)
{
	ENTER();

	struct mc_tpart *part = (struct mc_tpart *) arg;
	ASSERT(part->striding);

	mc_table_stride(part);

	part->striding = false;

	LEAVE();
	return 0;
}

static void
mc_table_start_striding(struct mc_tpart *part)
{
	ENTER();

#if ENABLE_MEMCACHE_LOCKS
	mm_core_post(MM_CORE_NONE, mc_table_stride_routine, (mm_value_t) part);
#else
	mm_core_post(MM_CORE_SELF, mc_table_stride_routine, (mm_value_t) part);
#endif

	LEAVE();
}

/**********************************************************************
 * Entry eviction.
 **********************************************************************/

static bool
mc_table_is_eviction_victim(struct mc_entry *entry)
{
	if (entry->state == MC_ENTRY_USED_MIN)
		return true;
	if (entry->exp_time && entry->exp_time <= mm_core->time_manager.time)
		return true;
	return false;
}

bool
mc_table_evict(struct mc_tpart *part, uint32_t nrequired)
{
	ENTER();

	uint32_t nvictims = 0;
	struct mm_link victims;
	mm_link_init(&victims);

	mc_table_lock(part);

	uint32_t navailable = part->nentries;
	navailable -= part->nentries_free;
	navailable -= part->nentries_void;
	if (nrequired > navailable)
		nrequired = navailable;

	while (nvictims < nrequired) {
		struct mc_entry *hand = part->clock_hand;
		if (unlikely(hand == part->entries_end))
			hand = part->entries;

		uint8_t state = hand->state;
		if (state >= MC_ENTRY_USED_MIN && state <= MC_ENTRY_USED_MAX) {
			if (mc_table_is_eviction_victim(hand)) {
				char *key = mc_entry_getkey(hand);
				mc_table_remove(part, hand->hash, key, hand->key_len);
				mm_link_insert(&victims, &hand->link);
				++nvictims;
			} else {
				hand->state--;
			}
		}

		part->clock_hand = hand + 1;
	}

	mc_table_unlock(part);

	while (!mm_link_empty(&victims)) {
		struct mm_link *link = mm_link_delete_head(&victims);
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		mc_table_unref_entry(part, entry);
	}

	LEAVE();
	return (nvictims && nvictims == nrequired);
}

static mm_value_t
mc_table_evict_routine(mm_value_t arg)
{
	ENTER();

	struct mc_tpart *part = (struct mc_tpart *) arg;
	ASSERT(part->evicting);

	size_t reserve = MC_TABLE_VOLUME_RESERVE / mc_table.nparts;
	while (mc_table_check_volume(part, reserve) && mc_table_evict(part, 32))
		mm_task_yield();

	part->evicting = false;

	LEAVE();
	return 0;
}

static void
mc_table_start_evicting(struct mc_tpart *part)
{
	ENTER();

#if ENABLE_MEMCACHE_LOCKS
	mm_core_post(MM_CORE_NONE, mc_table_evict_routine, (mm_value_t) part);
#else
	mm_core_post(MM_CORE_SELF, mc_table_evict_routine, (mm_value_t) part);
#endif

	LEAVE();
}

/**********************************************************************
 * Table initialization and termination.
 **********************************************************************/

static void
mc_table_init_part(mm_core_t index, mm_core_t core)
{
	struct mc_tpart *part = &mc_table.parts[index];

	char *buckets = ((char *) mc_table.buckets_base)
			+ mc_table_buckets_size(index, mc_table.nbuckets_max);
	char *entries = ((char *) mc_table.entries_base)
			+ mc_table_entries_size(index, mc_table.nentries_max);

	part->buckets = (struct mm_link *) buckets;
	part->entries = (struct mc_entry *) entries;
	part->entries_end = part->entries;

	part->clock_hand = part->entries;

	mm_link_init(&part->free_list);

	part->nbuckets = 0;
	part->nbuckets = 0;
	part->nentries_free = 0;
	part->nentries_void = 0;

	part->volume = 0;

	mm_waitset_prepare(&part->waitset);
	mm_waitset_pin(&part->waitset, core);

#if ENABLE_MEMCACHE_LOCKS
	part->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;
#else
	mm_verbose("bind partition %d to core %d", index, core);
	part->core = core;
#endif

	part->evicting = false;
	part->striding = false;

	part->cas = index;

	// Allocate initial space for the table.
	mc_table_expand(part, mc_table.nentries_increment);
	uint32_t nbuckets = part->nentries / 2;
	mc_table_buckets_resize(part, 0, nbuckets);
	part->nbuckets = nbuckets;
}

void
mc_table_init(const struct mm_memcache_config *config)
{
	ENTER();

	// Round the number of table partitions to a power of 2.
	mm_core_t nparts;
#if ENABLE_MEMCACHE_LOCKS
	nparts = config->nparts;
#else
	nparts = mm_bitset_count(&config->affinity);
#endif
	ASSERT(nparts > 0);
	uint16_t nbits = sizeof(int) * 8 - 1 - mm_clz(nparts);
	nparts = 1 << nbits;

	mm_brief("memcache partitions: %d", nparts);
	mm_brief("memcache partition bits: %d", nbits);

	// Determine the size constraints for table partitions.
	size_t volume = config->volume / nparts;
	if (volume < MM_PAGE_SIZE)
		volume = MM_PAGE_SIZE;
	// Make a very liberal estimate that for an average table entry
	// the combined size of key and data might be as small as 20 bytes.
	size_t nentries_max = volume / (sizeof(struct mc_entry) + 20);
	size_t nbuckets_max = 1 << (sizeof(int) * 8 - 1 - mm_clz(nentries_max));

	mm_brief("memcache maximum data volume per partition: %lu",
		 (unsigned long) volume);
	mm_brief("memcache maximum number of entries per partition: %lu",
		 (unsigned long) nentries_max);
	mm_brief("memcache maximum number of buckets per partition: %lu",
		 (unsigned long) nbuckets_max);
	if (nentries_max != (uint32_t) nentries_max)
		mm_fatal(0, "too many entries");
	if (nbuckets_max != (uint32_t) nbuckets_max)
		mm_fatal(0, "too many buckets");

	// Reserve address space for table entries.
	size_t entries_size = mc_table_entries_size(nparts, nentries_max);
	mm_brief("memcache reserved entries for table: %ld bytes",
		 (unsigned long) entries_size);
	void *entries_base = mmap(NULL, entries_size, PROT_NONE,
				  MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (entries_base == MAP_FAILED)
		mm_fatal(errno, "mmap");

	// Reserve address space for table buckets.
	size_t buckets_size = mc_table_buckets_size(nparts, nbuckets_max);
	mm_brief("memcache reserved buckets for table: %ld bytes",
		 (unsigned long) buckets_size);
	void *buckets_base = mmap(NULL, buckets_size, PROT_NONE,
				  MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (buckets_base == MAP_FAILED)
		mm_fatal(errno, "mmap");

	// Compute the number of entries added on expansion.
	uint32_t nentries_increment = 4 * 1024;
	if (nparts == 1)
		nentries_increment *= 4;
	else if (nparts == 2)
		nentries_increment *= 2;

	// Initialize the table.
	mc_table.parts = mm_shared_calloc(nparts, sizeof(struct mc_tpart));
	mc_table.nparts = nparts;
	mc_table.part_bits = nbits;
	mc_table.part_mask = nparts - 1;
	mc_table.volume_max = volume;
	mc_table.nbuckets_max = nbuckets_max;
	mc_table.nentries_max = nentries_max;
	mc_table.nentries_increment = nentries_increment;
	mc_table.buckets_base = buckets_base;
	mc_table.entries_base = entries_base;

	// Initialize the table partitions.
#if ENABLE_MEMCACHE_LOCKS
	for (mm_core_t index = 0; index < nparts; index++) {
		mc_table_init_part(index, MM_CORE_NONE);
	}
#else
	mm_core_t index = 0;
	ASSERT(nparts <= mm_core_getnum());
	for (mm_core_t core = 0; core < mm_core_getnum(); core++) {
		if (mm_bitset_test(&mc_config.affinity, core)) {
			mc_table_init_part(index++, core);
		}
	}
#endif

	LEAVE();
}

void
mc_table_term(void)
{
	ENTER();

	// Free the table entries.
	for (mm_core_t p = 0; p < mc_table.nparts; p++) {
		struct mc_tpart *part = &mc_table.parts[p];
		for (uint32_t i = 0; i < part->nbuckets; i++) {
			struct mm_link *link = mm_link_head(&part->buckets[i]);
			while (link != NULL) {
				struct mc_entry *entry =
					containerof(link, struct mc_entry, link);
				link = link->next;

				mc_table_destroy_entry(part, entry);
			}
		}
	}

	// Free the table partitions.
	mm_shared_free(mc_table.parts);

	// Compute the reserved address space size.
	size_t space = mc_table_buckets_size(mc_table.nparts, mc_table.nbuckets_max);
	size_t entry_space = mc_table_entries_size(mc_table.nparts, mc_table.nentries_max);

	// Release the reserved address space.
	if (munmap(mc_table.buckets_base, space) < 0)
		mm_error(errno, "munmap");
	if (munmap(mc_table.entries_base, entry_space) < 0)
		mm_error(errno, "munmap");

	LEAVE();
}

/**********************************************************************
 * Table entry creation and destruction routines.
 **********************************************************************/

struct mc_entry *
mc_table_create_entry(struct mc_tpart *part)
{
	struct mc_entry *entry = NULL;

again:
	if (!mm_link_empty(&part->free_list)) {
		struct mm_link *link = mm_link_delete_head(&part->free_list);
		ASSERT(part->nentries_free);
		part->nentries_free--;
		entry = containerof(link, struct mc_entry, link);
	} else if (part->nentries_void) {
		part->nentries_void--;
		entry = part->entries_end++;
	} else {
		// TODO: optimize this case, unlock before table expansion
		// allowing concurrent lookup & remove calls.
		mc_table_expand(part, mc_table.nentries_increment);
		if (!part->nentries_void) {
			mc_table_unlock(part);
			mc_table_evict(part, 1);
			mc_table_lock(part);
		}
		goto again;
	}

	entry->state = MC_ENTRY_NOT_USED;

	return entry;
}

void
mc_table_destroy_entry(struct mc_tpart *part, struct mc_entry *entry)
{
	struct mm_link *link = mm_link_head(&entry->chunks);
	struct mm_chunk *chunks = containerof(link, struct mm_chunk, base.link);
	mm_core_reclaim_chain(chunks);

	ASSERT(entry->state == MC_ENTRY_NOT_USED);
	entry->state = MC_ENTRY_FREE;

	mm_link_insert(&part->free_list, &entry->link);
	part->nentries_free++;
}

/**********************************************************************
 * Table entry access routines.
 **********************************************************************/

static inline void
mc_table_access(struct mc_tpart *part __attribute__((unused)),
		     struct mc_entry *entry)
{
	uint8_t state = mm_memory_load(entry->state);
	if (state >= MC_ENTRY_USED_MIN && state < MC_ENTRY_USED_MAX)
		mm_memory_store(entry->state, state + 1);
}

struct mc_entry *
mc_table_lookup(struct mc_tpart *part, uint32_t hash, const char *key, uint8_t key_len)
{
	ENTER();
	struct mc_entry *found_entry = NULL;

	uint32_t index = mc_table_index(part, hash);
	struct mm_link *bucket = &part->buckets[index];

	struct mm_link *link = mm_link_head(bucket);
	while (link != NULL) {
		struct mc_entry *entry = containerof(link, struct mc_entry, link);
		char *entry_key = mc_entry_getkey(entry);
		if (hash == entry->hash
		    && key_len == entry->key_len
		    && !memcmp(key, entry_key, key_len)) {
			mc_table_access(part, entry);
			found_entry = entry;
			break;
		}
		link = link->next;
	}

	LEAVE();
	return found_entry;
}

struct mc_entry *
mc_table_remove(struct mc_tpart *part, uint32_t hash, const char *key, uint8_t key_len)
{
	ENTER();
	struct mc_entry *found_entry = NULL;

	uint32_t index = mc_table_index(part, hash);
	struct mm_link *pred = &part->buckets[index];

	while (!mm_link_is_last(pred)) {
		struct mm_link *link = pred->next;
		struct mc_entry *entry = containerof(link, struct mc_entry, link);

		char *entry_key = mc_entry_getkey(entry);
		if (hash == entry->hash
		    && key_len == entry->key_len
		    && !memcmp(key, entry_key, key_len)) {
			mm_link_cleave(pred, link->next);
			entry->state = MC_ENTRY_NOT_USED;

			part->volume -= mc_entry_size(entry);

			found_entry = entry;
			break; 
		}

		pred = link;
	}

	LEAVE();
	return found_entry;
}

void
mc_table_insert(struct mc_tpart *part, uint32_t hash,
		struct mc_entry *entry, uint8_t state)
{
	ENTER();

	uint32_t index = mc_table_index(part, hash);
	struct mm_link *bucket = &part->buckets[index];

	mm_link_insert(bucket, &entry->link);
	entry->state = state;

	entry->cas = part->cas;
	part->cas += mc_table.nparts;

	part->volume += mc_entry_size(entry);
	if (!part->evicting && mc_table_check_volume(part, 0)) {
		part->evicting = true;
		mc_table_start_evicting(part);
	}
	if (!part->striding && mc_table_check_size(part)) {
		part->striding = true;
		mc_table_start_striding(part);
	}

	LEAVE();
}
