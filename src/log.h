/*
 * log.h - MainMemory logging.
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

#ifndef LOG_H
#define LOG_H

#include "common.h"
#include <stdarg.h>

/**********************************************************************
 * Low-Level Logging Routines.
 **********************************************************************/

void mm_log_str(const char *str)
	__attribute__((nonnull(1)));

void mm_log_fmt(const char *restrict fmt, ...)
	__attribute__((format(printf, 1, 2)))
	__attribute__((nonnull(1)));

void mm_log_vfmt(const char *restrict fmt, va_list va)
	__attribute__((nonnull(1)));

void mm_log_relay(void);

size_t mm_log_flush(void);

/**********************************************************************
 * High-Level Logging Routines.
 **********************************************************************/

void mm_enable_verbose(bool value);
void mm_enable_warning(bool value);

void mm_brief(const char *restrict msg, ...)
	__attribute__((format(printf, 1, 2)))
	__attribute__((nonnull(1)));

void mm_verbose(const char *restrict msg, ...)
	__attribute__((format(printf, 1, 2)))
	__attribute__((nonnull(1)));

void mm_warning(int error, const char *restrict msg, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(2)));

void mm_error(int error, const char *restrict msg, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(2)));

void mm_fatal(int error, const char *restrict msg, ...)
	__attribute__((format(printf, 2, 3)))
	__attribute__((nonnull(2)))
	__attribute__((noreturn));

#endif /* LOG_H */
