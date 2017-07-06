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

/*
 * Settings is a central storage for runtime options obtained from command
 * line arguments and the configuration file.
 *
 * For an option to be properly parsed when it is met in the configuration
 * file it has to be registered in advance with the mm_settings_set_type()
 * function. The options that are described in the mm_args_info table (see
 * "args.h" file) are automatically registered. Any other options has to be
 * registered explicitly.
 */

typedef enum
{
	/* Unknown setting, silently skipped if met in the config. */
	MM_SETTINGS_UNKNOWN,
	/* A boolean setting, only boolean values are allowed. */
	MM_SETTINGS_BOOLEAN,
	/* A regular setting, any scalar values are allowed. */
	MM_SETTINGS_REGULAR,
} mm_settings_info_t;

/**********************************************************************
 * Settings subsystem initialization and configuration.
 **********************************************************************/

void
mm_settings_init(void);

void NONNULL(1)
mm_settings_set_info(const char *key, mm_settings_info_t info);

mm_settings_info_t NONNULL(1)
mm_settings_get_info(const char *key);

/**********************************************************************
 * Type-oblivious access to settings.
 **********************************************************************/

void NONNULL(1)
mm_settings_set(const char *key, const char *value, bool overwrite);

const char * NONNULL(1)
mm_settings_get(const char *key, const char *def);

/**********************************************************************
 * Type-conscious read-only access to settings.
 **********************************************************************/

bool NONNULL(1)
mm_settings_get_bool(const char *key, bool def);

uint32_t NONNULL(1)
mm_settings_get_uint32(const char *key, uint32_t def);

uint64_t NONNULL(1)
mm_settings_get_uint64(const char *key, uint64_t def);

#endif /* BASE_SETTINGS_H */
