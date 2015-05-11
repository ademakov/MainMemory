/*
 * memcache/entry.h - MainMemory memcache entries.
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

#ifndef MEMCACHE_ENTRY_H
#define MEMCACHE_ENTRY_H

#include "memcache/memcache.h"

#include "base/list.h"
#include "base/mem/chunk.h"
#include "core/core.h"

#if !ENABLE_MEMCACHE_COMBINER
# include "arch/atomic.h"
#endif

/* Forward declaration. */
struct mc_action;

#define MC_ENTRY_FREE		0
#define MC_ENTRY_USED_MIN	1
#define MC_ENTRY_USED_MAX	32
#define MC_ENTRY_NOT_USED	255

struct mc_entry
{
	struct mm_link link;
	struct mm_link chunks;

	uint32_t hash;
	uint32_t exp_time;
	uint32_t flags;

#if ENABLE_MEMCACHE_COMBINER
	uint16_t ref_count;
#else
	mm_atomic_uint16_t ref_count;
#endif

	uint8_t state;

	uint8_t key_len;
	uint32_t value_len;
	uint64_t stamp;
};

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
	struct mm_link *link = mm_link_head(&entry->chunks);
	struct mm_chunk *chunk = containerof(link, struct mm_chunk, base.link);
	return chunk->data;
}

static inline void
mc_entry_setkey(struct mc_entry *entry, const char *key)
{
	char *entry_key = mc_entry_getkey(entry);
	memcpy(entry_key, key, entry->key_len);
}

static inline char *
mc_entry_getvalue(struct mc_entry *entry)
{
	struct mm_link *link = mm_link_head(&entry->chunks);
	struct mm_chunk *chunk = containerof(link, struct mm_chunk, base.link);
	return chunk->data + entry->key_len;
}

static inline void
mc_entry_alloc_chunks(struct mc_entry *entry)
{
	ASSERT(mm_link_empty(&entry->chunks));
	size_t size = mc_entry_size(entry);
	struct mm_chunk *chunk = mm_chunk_create(mm_core_selfid(), size);
	mm_link_insert(&entry->chunks, &chunk->base.link);
}

static inline void
mc_entry_free_chunks(struct mc_entry *entry)
{
	mm_chunk_destroy_chain(mm_link_head(&entry->chunks));
}

void __attribute__((nonnull(1, 2)))
mc_entry_setnum(struct mc_entry *entry, struct mc_action *action, uint64_t value);

bool __attribute__((nonnull(1, 2)))
mc_entry_getnum(struct mc_entry *entry, uint64_t *value);

#endif /* MEMCACHE_ENTRY_H */
