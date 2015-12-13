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
#include "base/scan.h"
#include "base/log/error.h"
#include "base/memory/global.h"

struct mm_settings_entry
{
	struct mm_hashmap_entry entry;
	const char *value;
	mm_settings_type_t type;
};

static const char *mm_settings_empty = "";

static struct mm_hashmap mm_settings_map;

static void
mm_settings_map_free(struct mm_hashmap *map UNUSED, struct mm_hashmap_entry *hep)
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

void NONNULL(1)
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

const char * NONNULL(1)
mm_settings_get(const char *key, const char *def)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (hep != NULL) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		if (sep->value != NULL)
			return sep->value;
	}
	return def;
}

bool NONNULL(1)
mm_settings_getbool(const char *key, const char *def)
{
	bool val = false;
	const char *str = mm_settings_get(key, def);
	if (str != NULL) {
		int err = 0;
		str = mm_scan_bool(&val, &err, str, NULL);
		if (err || *str)
			mm_fatal(err, "invalid '%s' setting: '%s'", key, str);
	}
	return val;
}

uint32_t NONNULL(1)
mm_settings_get_uint32(const char *key, const char *def)
{
	uint32_t val = 0;
	const char *str = mm_settings_get(key, def);
	if (str != NULL) {
		int err = 0;
		str = mm_scan_n32(&val, &err, str, NULL);
		if (err || *str)
			mm_fatal(err, "invalid '%s' setting: '%s'", key, str);
	}
	return val;
}

uint64_t NONNULL(1)
mm_settings_get_uint64(const char *key, const char *def)
{
	uint64_t val = 0;
	const char *str = mm_settings_get(key, def);
	if (str != NULL) {
		int err = 0;
		str = mm_scan_n64(&val, &err, str, NULL);
		if (err || *str)
			mm_fatal(err, "invalid '%s' setting: '%s'", key, str);
	}
	return val;
}

void NONNULL(1)
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

mm_settings_type_t NONNULL(1)
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
