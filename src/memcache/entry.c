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

#include "memcache/entry.h"
#include "memcache/table.h"

#include "core/core.h"

#include <ctype.h>

void
mc_entry_set(struct mc_entry *entry, uint32_t hash,
	     uint8_t key_len, const char *key,
	     uint32_t flags, uint32_t exp_time,
	     uint32_t value_len)
{
	entry->hash = hash;
	entry->key_len = key_len;
	entry->value_len = value_len;
	entry->flags = flags;
	entry->exp_time = exp_time;
	entry->ref_count = 1;

	mm_link_init(&entry->chunks);
	size_t size = mc_entry_sum_length(key_len, value_len);
	struct mm_chunk *chunk = mm_chunk_create(size);
	mm_link_insert(&entry->chunks, &chunk->base.link);

	char *entry_key = mc_entry_getkey(entry);
	memcpy(entry_key, key, entry->key_len);
}

void
mc_entry_setnum(struct mc_entry *entry, uint32_t hash,
		uint8_t key_len, const char *key,
		uint32_t flags, uint32_t exp_time,
		uint64_t value)
{
	char buffer[32];
	size_t value_len = 0;
	do {
		int c = (int) (value % 10);
		buffer[value_len++] = '0' + c;
		value /= 10;
	} while (value);

	mc_entry_set(entry, hash, key_len, key, flags, exp_time, value_len);

	char *v = mc_entry_getvalue(entry);
	do {
		size_t i = entry->value_len - value_len--;
		v[i] = buffer[value_len];
	} while (value_len);
}

bool
mc_entry_getnum(struct mc_entry *entry, uint64_t *value)
{
	if (entry->value_len == 0)
		return false;

	char *p = mc_entry_getvalue(entry);
	char *e = p + entry->value_len;

	uint64_t v = 0;
	while (p < e) {
		int c = *p++;
		if (!isdigit(c))
			return false;

		uint64_t vv = v * 10 + c - '0';
		if (unlikely(vv < v))
			return false;

		v = vv;
	}

	*value = v;
	return true;
}
