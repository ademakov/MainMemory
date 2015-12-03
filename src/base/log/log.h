/*
 * base/log/log.h - MainMemory logging.
 *
 * Copyright (C) 2013-2014  Aleksey Demakov
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

#ifndef BASE_LOG_LOG_H
#define BASE_LOG_LOG_H

#include "common.h"
#include <stdarg.h>

void NONNULL(1)
mm_log_str(const char *str);

void NONNULL(1) FORMAT(1, 2)
mm_log_fmt(const char *restrict fmt, ...);

void NONNULL(1)
mm_log_vfmt(const char *restrict fmt, va_list va);

void mm_log_relay(void);

size_t mm_log_flush(void);

#endif /* BASE_LOG_LOG_H */
