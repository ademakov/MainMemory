/*
 * base/log/error.h - MainMemory error message logging.
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

#ifndef BASE_LOG_ERROR_H
#define BASE_LOG_ERROR_H

#include "common.h"

void
mm_set_warning_enabled(bool value);

bool
mm_get_warning_enabled(void);

void NONNULL(2) FORMAT(2, 3)
mm_warning(int error, const char *restrict msg, ...);

void NONNULL(2) FORMAT(2, 3)
mm_error(int error, const char *restrict msg, ...);

void NONNULL(2) FORMAT(2, 3) NORETURN
mm_fatal(int error, const char *restrict msg, ...);

#endif /* BASE_LOG_ERROR_H */
