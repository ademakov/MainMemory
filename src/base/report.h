/*
 * base/report.h - MainMemory message logging.
 *
 * Copyright (C) 2012-2016  Aleksey Demakov
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

#ifndef BASE_REPORT_H
#define BASE_REPORT_H

#include "common.h"

/**********************************************************************
 * Message verbosity control.
 **********************************************************************/

void
mm_set_verbose_enabled(bool value);

void
mm_set_warning_enabled(bool value);

bool
mm_get_verbose_enabled(void);

bool
mm_get_warning_enabled(void);

/**********************************************************************
 * Plain info messages.
 **********************************************************************/

void NONNULL(1) FORMAT(1, 2)
mm_verbose(const char *restrict msg, ...);

void NONNULL(1) FORMAT(1, 2)
mm_brief(const char *restrict msg, ...);

/**********************************************************************
 * Error messages.
 **********************************************************************/

void NONNULL(2) FORMAT(2, 3)
mm_warning(int error, const char *restrict msg, ...);

void NONNULL(2) FORMAT(2, 3)
mm_error(int error, const char *restrict msg, ...);

void NONNULL(2) FORMAT(2, 3) NORETURN
mm_fatal(int error, const char *restrict msg, ...);

#endif /* BASE_REPORT_H */
