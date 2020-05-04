/*
 * base/settings.c - MainMemory settings.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#include "base/exit.h"
#include "base/hashmap.h"
#include "base/report.h"
#include "base/scan.h"
#include "base/memory/alloc.h"
#include "base/memory/arena.h"

struct mm_settings_entry
{
	struct mm_hashmap_entry entry;
	const char *value;
	mm_settings_info_t info;
};

static const char *mm_settings_empty = "";

static struct mm_hashmap mm_settings_map;

static void
mm_settings_map_free(struct mm_hashmap *map UNUSED, struct mm_hashmap_entry *hep)
{
	struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
	mm_memory_free((char *) sep->entry.key);
	if (sep->value != NULL && sep->value != mm_settings_empty)
		mm_memory_free((char *) sep->value);
	mm_memory_free(sep);
}

static void
mm_settings_cleanup(void)
{
	ENTER();

	mm_hashmap_cleanup(&mm_settings_map, mm_settings_map_free);

	LEAVE();
}

/**********************************************************************
 * Settings subsystem initialization and configuration.
 **********************************************************************/

void
mm_settings_init(void)
{
	ENTER();

	mm_hashmap_prepare(&mm_settings_map, &mm_memory_xarena);
	mm_atexit(mm_settings_cleanup);

	LEAVE();
}


void NONNULL(1)
mm_settings_set_info(const char *key, mm_settings_info_t info)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (hep != NULL) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		sep->info = info;
	} else {
		struct mm_settings_entry *sep = mm_memory_xalloc(sizeof(struct mm_settings_entry));
		mm_hashmap_setkey(&sep->entry, mm_memory_memdup(key, len), len);
		mm_hashmap_insert(&mm_settings_map, &sep->entry);
		sep->value = NULL;
		sep->info = info;
	}
}

mm_settings_info_t NONNULL(1)
mm_settings_get_info(const char *key)
{
	size_t len = strlen(key);
	struct mm_hashmap_entry *hep = mm_hashmap_lookup(&mm_settings_map, key, len);
	if (hep != NULL) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		return sep->info;
	}
	return MM_SETTINGS_UNKNOWN;
}

/**********************************************************************
 * Type-oblivious access to settings.
 **********************************************************************/

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
					mm_memory_free((char *) sep->value);
			}
		} else {
			sep = mm_memory_xalloc(sizeof(struct mm_settings_entry));
			mm_hashmap_setkey(&sep->entry, mm_memory_memdup(key, len), len);
			mm_hashmap_insert(&mm_settings_map, &sep->entry);
			sep->info = MM_SETTINGS_UNKNOWN;
		}
		sep->value = *value ? mm_memory_strdup(value) : mm_settings_empty;
	} else if (hep != NULL && !overwrite) {
		struct mm_settings_entry *sep = containerof(hep, struct mm_settings_entry, entry);
		if (sep->info != MM_SETTINGS_UNKNOWN && sep->value != NULL) {
			if (sep->value != mm_settings_empty)
				mm_memory_free((char *) sep->value);
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

/**********************************************************************
 * Type-conscious read-only access to settings.
 **********************************************************************/

bool NONNULL(1)
mm_settings_get_bool(const char *key, bool def)
{
	bool val = def;
	const char *str = mm_settings_get(key, NULL);
	if (str != NULL) {
		int err = 0;
		str = mm_scan_bool(&val, &err, str, NULL);
		if (err || *str)
			mm_fatal(err, "invalid '%s' setting: '%s'", key, str);
	}
	return val;
}

uint32_t NONNULL(1)
mm_settings_get_uint32(const char *key, uint32_t def)
{
	uint32_t val = def;
	const char *str = mm_settings_get(key, NULL);
	if (str != NULL) {
		int err = 0;
		str = mm_scan_n32(&val, &err, str, NULL);
		if (err || *str)
			mm_fatal(err, "invalid '%s' setting: '%s'", key, str);
	}
	return val;
}

uint64_t NONNULL(1)
mm_settings_get_uint64(const char *key, uint64_t def)
{
	uint64_t val = def;
	const char *str = mm_settings_get(key, NULL);
	if (str != NULL) {
		int err = 0;
		str = mm_scan_n64(&val, &err, str, NULL);
		if (err || *str)
			mm_fatal(err, "invalid '%s' setting: '%s'", key, str);
	}
	return val;
}
