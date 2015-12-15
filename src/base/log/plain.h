/*
 * base/log/plain.h - MainMemory plain message logging.
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

#ifndef BASE_LOG_PLAIN_H
#define BASE_LOG_PLAIN_H

#include "common.h"

void
mm_set_verbose_enabled(bool value);

bool
mm_get_verbose_enabled(void);

void NONNULL(1) FORMAT(1, 2)
mm_verbose(const char *restrict msg, ...);

void NONNULL(1) FORMAT(1, 2)
mm_brief(const char *restrict msg, ...);

#endif /* BASE_LOG_PLAIN_H */
