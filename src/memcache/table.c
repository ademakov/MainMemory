/*
 * table.c - MainMemory memcache entry table.
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

#include "table.h"
#include "entry.h"

#include "../hash.h"
#include "../log.h"
#include "../task.h"

#include <sys/mman.h>

#define MC_TABLE_STRIDE		64

#if MM_WORD_32BIT
# define MC_TABLE_SIZE_MAX	((size_t) 64 * 1024 * 1024)
#else
# define MC_TABLE_SIZE_MAX	((size_t) 512 * 1024 * 1024)
#endif

#define MC_TABLE_VOLUME_MAX	(64 * 1024 * 1024)
#define MC_TABLE_VOLUME_RESERVE	(64 * 1024)

struct mc_table mc_table;

/**********************************************************************
 * Memcache table helper routines.
 **********************************************************************/

static inline size_t
mc_table_space(size_t nbuckets)
{
	return nbuckets * sizeof(struct mm_link);
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
	uint32_t n = mm_memory_load(part->nbuckets);
	return part->nentries > (n * 2) && n < mc_table.nbuckets_max;
}

static inline bool
mc_table_check_volume(struct mc_tpart *part, size_t reserve)
{
	uint32_t n = mm_memory_load(part->nbytes);
	return (n + reserve) > mc_table.nbytes_threshold;
}

/**********************************************************************
 * Memcache table growth.
 **********************************************************************/

static void
mc_table_expand(struct mc_tpart *part, uint32_t old_size, uint32_t new_size)
{
	ENTER();
	ASSERT(mm_is_pow2z(old_size));
	ASSERT(mm_is_pow2(new_size));

	mm_brief("Set the size of memcache partition #%d: %ld",
		 (int) (part - mc_table.parts), (unsigned long) new_size);

	size_t old_space = mc_table_space(old_size);
	size_t new_space = mc_table_space(new_size);

	void *address = (char *) part->buckets + old_space;
	size_t nbytes = new_space - old_space;

	void *result_address = mmap(address, nbytes, PROT_READ | PROT_WRITE,
				    MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
	if (result_address == MAP_FAILED)
		mm_fatal(errno, "mmap");
	if (result_address != address)
		mm_fatal(0, "mmap returned wrong address");

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
		mc_table_expand(part, used, used * 2);
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

			struct mc_entry *entry =
				containerof(link, struct mc_entry, table_link);
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
 * Memcache table eviction.
 **********************************************************************/

bool
mc_table_evict(struct mc_tpart *part, size_t nrequired)
{
	ENTER();

	size_t nvictims = 0;
	struct mm_link victims;
	mm_link_init(&victims);

	mc_table_lock(part);

	while (!mm_list_empty(&part->evict_list)) {
		struct mm_list *link = mm_list_head(&part->evict_list);
		struct mc_entry *entry = containerof(link, struct mc_entry, evict_list);
		char *key = mc_entry_getkey(entry);

		mc_table_remove(part, entry->hash, key, entry->key_len);

		mm_link_insert(&victims, &entry->table_link);
		if (++nvictims == nrequired)
			break;
	}

	mc_table_unlock(part);

	while (!mm_link_empty(&victims)) {
		struct mm_link *link = mm_link_delete_head(&victims);
		struct mc_entry *entry = containerof(link, struct mc_entry, table_link);
		mc_entry_unref(entry);
	}

	LEAVE();
	return (nvictims == nrequired);
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
 * Memcache table initialization and termination.
 **********************************************************************/

static void
mc_table_init_part(mm_core_t index, mm_core_t core, struct mm_link *buckets)
{
	struct mc_tpart *part = &mc_table.parts[index];

#if ENABLE_MEMCACHE_LOCKS
	part->lock = (mm_task_lock_t) MM_TASK_LOCK_INIT;
	(void) core;
#else
	mm_verbose("bind partition %d to core %d", index, core);
	part->core = core;
#endif
	part->evicting = false;
	part->striding = false;
	part->cas = index;
	part->nentries = 0;
	part->buckets = buckets;
	mm_list_init(&part->evict_list);
	part->nbytes = 0;

	// Compute the initial table size
	uint32_t size = MM_PAGE_SIZE / sizeof(struct mc_entry *);

	// Allocate initial space for the table.
	mc_table_expand(part, 0, size);
	part->nbuckets = size;
}

void
mc_table_init(const struct mm_memcache_config *config)
{
	ENTER();

	// Compute the number of table partitions. It has to be a power of 2.
	mm_core_t nparts;
#if ENABLE_MEMCACHE_LOCKS
	nparts = config->nparts;
#else
	nparts = mm_bitset_count(&config->affinity);
#endif
	ASSERT(nparts > 0);
	uint16_t nbits = 31 - mm_clz(nparts);
	nparts = 1 << nbits;

	// Reserve the address space for the table.
	size_t space = mc_table_space(MC_TABLE_SIZE_MAX);
	mm_brief("reserve %ld bytes of the address space for the memcache table.",
		 (unsigned long) space);
	void *address = mmap(NULL, space, PROT_NONE,
			     MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (address == MAP_FAILED)
		mm_fatal(errno, "mmap");

	// Initialize the table.
	mc_table.nparts = nparts;
	mc_table.address = address;
	mc_table.part_bits = nbits;
	mc_table.part_mask = nparts - 1;
	mc_table.nbuckets_max = MC_TABLE_SIZE_MAX / nparts;
	mc_table.nbytes_threshold = MC_TABLE_VOLUME_MAX / nparts;
	mc_table.parts = mm_shared_calloc(nparts, sizeof(struct mc_tpart));

	mm_brief("memcache partitions: %d", nparts);
	mm_brief("memcache partition bits: %d", mc_table.part_bits);

	// Initialize the table partitions.
	struct mm_link *base = address;
#if ENABLE_MEMCACHE_LOCKS
	for (mm_core_t index = 0; index < nparts; index++) {
		struct mm_link *buckets = base + index * mc_table.nbuckets_max;
		mc_table_init_part(index, MM_CORE_NONE, buckets);
	}
#else
	mm_core_t index = 0;
	for (mm_core_t core = 0; core < mm_core_getnum(); core++) {
		if (mm_bitset_test(&mc_config.affinity, core)) {
			struct mc_entry **buckets = base + index * mc_table.nbuckets_max;
			mc_table_init_part(index, core, buckets);
			++index;
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
					containerof(link, struct mc_entry, table_link);
				link = link->next;

				mm_shared_free(entry);
			}
		}
	}

	// Free the table partitions.
	mm_shared_free(mc_table.parts);

	// Compute the reserved address space size.
	size_t space = mc_table_space(MC_TABLE_SIZE_MAX);

	// Release the reserved address space.
	if (munmap(mc_table.address, space) < 0)
		mm_error(errno, "munmap");

	LEAVE();
}

/**********************************************************************
 * Memcache table access routines.
 **********************************************************************/

struct mc_entry *
mc_table_lookup(struct mc_tpart *part, uint32_t hash, const char *key, uint8_t key_len)
{
	ENTER();
	struct mc_entry *found_entry = NULL;

	uint32_t index = mc_table_index(part, hash);
	struct mm_link *bucket = &part->buckets[index];

	struct mm_link *link = mm_link_head(bucket);
	while (link != NULL) {
		struct mc_entry *entry = containerof(link, struct mc_entry, table_link);
		char *entry_key = mc_entry_getkey(entry);
		if (hash == entry->hash
		    && key_len == entry->key_len
		    && !memcmp(key, entry_key, key_len)) {
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
		struct mc_entry *entry = containerof(link, struct mc_entry, table_link);

		char *entry_key = mc_entry_getkey(entry);
		if (hash == entry->hash
		    && key_len == entry->key_len
		    && !memcmp(key, entry_key, key_len)) {
			mm_list_delete(&entry->evict_list);
			mm_link_cleave(pred, link->next);

			part->nbytes -= mc_entry_size(entry);
			part->nentries--;

			found_entry = entry;
			break; 
		}

		pred = link;
	}

	LEAVE();
	return found_entry;
}

void
mc_table_insert(struct mc_tpart *part, uint32_t hash, struct mc_entry *entry)
{
	ENTER();

	uint32_t index = mc_table_index(part, hash);
	struct mm_link *bucket = &part->buckets[index];

	mm_link_insert(bucket, &entry->table_link);
	mm_list_append(&part->evict_list, &entry->evict_list);

	part->nbytes += mc_entry_size(entry);
	part->nentries++;

	entry->cas = part->cas;
	part->cas += mc_table.nparts;

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
