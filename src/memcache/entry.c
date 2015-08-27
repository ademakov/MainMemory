/*
 * memcache/entry.c - MainMemory memcache entries.
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

#include "memcache/entry.h"
#include "memcache/action.h"

#include <ctype.h>

void __attribute__((nonnull(1, 2)))
mc_entry_setnum(struct mc_entry *entry, struct mc_action *action, uint64_t value)
{
	char buffer[MC_ENTRY_NUM_LEN_MAX];
	size_t value_len = 0;
	do {
		int c = (int) (value % 10);
		buffer[value_len++] = '0' + c;
		value /= 10;
	} while (value);

	entry->value_len = value_len;
	mc_entry_alloc_chunks(entry);
	mc_entry_setkey(entry, action->key);

	char *v = mc_entry_getvalue(entry);
	do {
		size_t i = entry->value_len - value_len--;
		v[i] = buffer[value_len];
	} while (value_len);
}

bool __attribute__((nonnull(1, 2)))
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
