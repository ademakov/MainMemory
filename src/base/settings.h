/*
 * base/settings.h - MainMemory settings.
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

#ifndef BASE_SETTINGS_H
#define BASE_SETTINGS_H

#include "common.h"

typedef enum
{
	MM_SETTINGS_UNKNOWN,
	MM_SETTINGS_TRIVIAL,
	MM_SETTINGS_REGULAR,
} mm_settings_type_t;

void
mm_settings_init(void);

void NONNULL(1)
mm_settings_set(const char *key, const char *value, bool overwrite);

const char * NONNULL(1)
mm_settings_get(const char *key, const char *def);

bool NONNULL(1)
mm_settings_get_bool(const char *key, bool def);

uint32_t NONNULL(1)
mm_settings_get_uint32(const char *key, uint32_t def);

uint64_t NONNULL(1)
mm_settings_get_uint64(const char *key, uint64_t def);

void NONNULL(1)
mm_settings_settype(const char *key, mm_settings_type_t type);

mm_settings_type_t NONNULL(1)
mm_settings_gettype(const char *key);

#endif /* BASE_SETTINGS_H */
