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
	const char *value;
	mm_settings_type_t type;
};

static const char *mm_settings_empty = "";

static struct mm_hashmap mm_settings_map;

static void
mm_settings_map_free(struct mm_hashmap *map __mm_unused__, struct mm_hashmap_entry *hep)
{
	struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
	mm_global_free((char *) sep->entry.key);
	if (sep->value != NULL && sep->value != mm_settings_empty)
		mm_global_free((char *) sep->value);
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
	mm_hashmap_cleanup(&mm_settings_map, mm_settings_map_free);
}

void __attribute__((nonnull(1)))
mm_settings_set(const char *key, const char *value, bool overwrite)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (value != NULL) {
		struct mm_settings_entry *sep;
		if (hep != NULL) {
			sep = containerof(hep, struct mm_settings_entry, entry);
			if (sep->value != NULL) {
				if (!overwrite)
					return;
				if (sep->value != mm_settings_empty)
					mm_global_free((char *) sep->value);
			}
		} else {
			sep = mm_global_alloc(sizeof(struct mm_settings_entry));
			mm_hashmap_setkey(&sep->entry, mm_global_memdup(key, len), len);
			mm_hashmap_insert(&mm_settings_map, &sep->entry);
			sep->type = MM_SETTINGS_UNKNOWN;
		}
		sep->value = *value ? mm_global_strdup(value) : mm_settings_empty;
	} else if (hep != NULL && !overwrite) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		if (sep->type != MM_SETTINGS_UNKNOWN && sep->value != NULL) {
			if (sep->value != mm_settings_empty)
				mm_global_free((char *) sep->value);
			sep->value = NULL;
		} else {
			mm_hashmap_remove(&mm_settings_map, hep);
			mm_settings_map_free(&mm_settings_map, hep);
		}
	}
}

const char * __attribute__((nonnull(1)))
mm_settings_get(const char *key, const char *value)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (hep != NULL) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		if (sep->value != NULL)
			value = sep->value;
	}
	return value;
}

void __attribute__((nonnull(1)))
mm_settings_settype(const char *key, mm_settings_type_t type)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (hep != NULL) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		sep->type = type;
	} else {
		struct mm_settings_entry *sep = mm_global_alloc(sizeof(struct mm_settings_entry));
		mm_hashmap_setkey(&sep->entry, mm_global_memdup(key, len), len);
		mm_hashmap_insert(&mm_settings_map, &sep->entry);
		sep->value = NULL;
		sep->type = type;
	}
}

mm_settings_type_t __attribute__((nonnull(1)))
mm_settings_gettype(const char *key)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (hep != NULL) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		return sep->type;
	}
	return MM_SETTINGS_UNKNOWN;
}
