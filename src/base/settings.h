/*
 * base/settings.h - MainMemory settings.
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

#ifndef BASE_SETTINGS_H
#define BASE_SETTINGS_H

void
mm_settings_init(void);

void
mm_settings_term(void);

void __attribute__((nonnull(1)))
mm_settings_put(const char *key, const char *value);

const char * __attribute__((nonnull(1)))
mm_settings_get(const char *key, const char *value);

#endif /* BASE_SETTINGS_H */
