/*
 * entry.h - MainMemory memcache entries.
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

#ifndef MEMCACHE_ENTRY_H
#define MEMCACHE_ENTRY_H

#include "memcache.h"
#include "list.h"

struct mc_entry
{
	struct mm_link table_link;
	struct mm_list evict_list;

	uint8_t key_len;
	uint32_t value_len;

	mm_atomic_uint32_t ref_count;
#if ENABLE_MEMCACHE_INDEX_DEBUG
	uint32_t index;
#endif

	uint32_t flags;
	uint64_t cas;
	char data[];
};

struct mc_entry * mc_entry_create(uint8_t key_len, size_t value_len);

static inline void
mc_entry_destroy(struct mc_entry *entry)
{
	mm_shared_free(entry);
}

struct mc_entry * mc_entry_create_u64(uint8_t key_len, uint64_t value);

bool mc_entry_value_u64(struct mc_entry *entry, uint64_t *value)
	__attribute__((nonnull(1, 2)));

static inline size_t
mc_entry_sum_length(uint8_t key_len, size_t value_len)
{
	return sizeof(struct mc_entry) + key_len + value_len;
}

static inline size_t
mc_entry_size(struct mc_entry *entry)
{
	return mc_entry_sum_length(entry->key_len, entry->value_len);
}

static inline char *
mc_entry_getkey(struct mc_entry *entry)
{
	return entry->data;
}

static inline char *
mc_entry_getvalue(struct mc_entry *entry)
{
	return entry->data + entry->key_len;
}

static inline void
mc_entry_setkey(struct mc_entry *entry, const char *key)
{
	char *entry_key = mc_entry_getkey(entry);
	memcpy(entry_key, key, entry->key_len);
}

static inline void
mc_entry_ref(struct mc_entry *entry)
{
	uint32_t test;
#if ENABLE_SMP
	test = mm_atomic_uint32_inc_and_test(&entry->ref_count);
#else
	test = ++(entry->ref_count);
#endif
	if (unlikely(!test)) {
		ABORT();
	}
}

static inline void
mc_entry_unref(struct mc_entry *entry)
{
	uint32_t test;
#if ENABLE_SMP
	test = mm_atomic_uint32_dec_and_test(&entry->ref_count);
#else
	test = --(entry->ref_count);
#endif
	if (!test) {
		mc_entry_destroy(entry);
	}
}

#endif /* MEMCACHE_ENTRY_H */
