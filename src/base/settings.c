/*
 * base/settings.c - MainMemory settings.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "base/settings.h"

#include "base/hashmap.h"
#include "base/log/error.h"
#include "base/memory/alloc.h"
#include "base/memory/memory.h"

struct mm_settings_entry
{
	struct mm_hashmap_entry entry;
	char *value;
};

static struct mm_hashmap mm_settings_map;

static void
mm_settings_free(struct mm_hashmap *map __mm_unused__, struct mm_hashmap_entry *hep)
{
	struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
	mm_global_free((char *) sep->entry.key);
	if (sep->value != NULL)
		mm_global_free(sep->value);
	mm_global_free(sep);
}

void
mm_settings_init(void)
{
	mm_hashmap_prepare(&mm_settings_map, &mm_global_arena);
}

void
mm_settings_term(void)
{
	mm_hashmap_cleanup(&mm_settings_map, mm_settings_free);
}

void __attribute__((nonnull(1)))
mm_settings_put(const char *key, const char *value)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (value == NULL) {
		if (hep != NULL) {
			mm_hashmap_remove(&mm_settings_map, hep);
			mm_settings_free(&mm_settings_map, hep);
		}
	} else {
		struct mm_settings_entry *sep;
		if (hep != NULL) {
			sep = containerof(hep, struct mm_settings_entry, entry);
			if (sep->value != NULL)
				mm_global_free((char *) sep->value);
		} else {
			sep = mm_global_alloc(sizeof(struct mm_settings_entry));
			mm_hashmap_setkey(&sep->entry, mm_global_memdup(key, len), len);
			mm_hashmap_insert(&mm_settings_map, &sep->entry);
		}
		sep->value = *value ? mm_global_strdup(value) : NULL;
	}
}

const char * __attribute__((nonnull(1)))
mm_settings_get(const char *key, const char *value)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (hep != NULL) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		value = sep->value != NULL ? sep->value : "";
	}
	return value;
}
