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
#include "base/memory/alloc.h"
#include "core/core.h"

#if !ENABLE_MEMCACHE_COMBINER
# include "base/atomic.h"
#endif

/* Forward declaration. */
struct mc_action;

#define MC_ENTRY_FREE		0
#define MC_ENTRY_USED_MIN	1
#define MC_ENTRY_USED_MAX	32
#define MC_ENTRY_NOT_USED	255

#define MC_ENTRY_NUM_LEN_MAX	20

struct mc_entry
{
	struct mm_slink link;
	char *data;

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

static inline uint32_t
mc_entry_fix_exptime(uint32_t exptime)
{
	if (exptime != 0 && exptime <= (60 * 60 * 24 * 30)) {
		struct mm_core *core = mm_core_selfptr();
		exptime += mm_core_getrealtime(core) / 1000000;
	}
	return exptime;
}

static inline uint32_t
mc_entry_size(struct mc_entry *entry)
{
	return (sizeof(struct mc_entry)
		+ entry->key_len + entry->value_len
		+ MM_ALLOC_OVERHEAD);
}

static inline char *
mc_entry_getkey(struct mc_entry *entry)
{
	return entry->data;
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
	return entry->data + entry->key_len;
}

void NONNULL(1)
mc_entry_setnum(struct mc_entry *entry, uint64_t value);

bool NONNULL(1, 2)
mc_entry_getnum(struct mc_entry *entry, uint64_t *value);

#endif /* MEMCACHE_ENTRY_H */
