/*
 * memcache/entry.c - MainMemory memcache entries.
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

#include "entry.h"

#include "../bitops.h"
#include "../chunk.h"
#include "../core.h"

#include <ctype.h>

static struct mc_entry *
mc_entry_convert_chunk(struct mm_chunk *chunk)
{
	mm_core_t core = chunk->base.core;
	struct mc_entry *entry = (struct mc_entry *) chunk;
	entry->chunk_core = core;
	return entry;
}

static struct mm_chunk *
mc_entry_restore_chunk(struct mc_entry *entry)
{
	mm_core_t core = entry->chunk_core;
	struct mm_chunk *chunk = (struct mm_chunk *) entry;
	chunk->base.core = core;
	return chunk;
}

struct mc_entry *
mc_entry_create(uint8_t key_len, size_t value_len)
{
	DEBUG("key_len = %d, value_len = %ld", key_len, (long) value_len);

	size_t size = mc_entry_sum_length(key_len, value_len);
	struct mm_chunk *chunk = mm_chunk_create(size - sizeof(struct mm_chunk));
	struct mc_entry *entry = mc_entry_convert_chunk(chunk);
	entry->key_len = key_len;
	entry->value_len = value_len;
	entry->ref_count = 1;

	return entry;
}

void
mc_entry_destroy(struct mc_entry *entry)
{
	struct mm_chunk *chunk = mc_entry_restore_chunk(entry);
	mm_core_reclaim_chunk(chunk);
}

struct mc_entry *
mc_entry_create_u64(uint8_t key_len, uint64_t value)
{
	char buffer[32];

	size_t value_len = 0;
	do {
		int c = (int) (value % 10);
		buffer[value_len++] = '0' + c;
		value /= 10;
	} while (value);

	struct mc_entry *entry = mc_entry_create(key_len, value_len);
	char *v = mc_entry_getvalue(entry);
	do {
		size_t i = entry->value_len - value_len--;
		v[i] = buffer[value_len];
	} while (value_len);

	return entry;
}

bool
mc_entry_value_u64(struct mc_entry *entry, uint64_t *value)
{
	if (entry->value_len == 0) {
		return false;
	}

	char *p = mc_entry_getvalue(entry);
	char *e = p + entry->value_len;

	uint64_t v = 0;
	while (p < e) {
		int c = *p++;
		if (!isdigit(c)) {
			return false;
		}

		uint64_t vv = v * 10 + c - '0';
		if (unlikely(vv < v)) {
			return false;
		}

		v = vv;
	}

	*value = v;
	return true;
}
