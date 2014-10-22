/*
 * base/log/debug.h - MainMemory debug utilities.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef BASE_LOG_DEBUG_H
#define BASE_LOG_DEBUG_H

#include "common.h"

#define ABORT()		mm_abort_with_message(__LOCATION__, __FUNCTION__, "ABORT")

#if ENABLE_DEBUG
# define ASSERT(e)	(likely(e) ? (void)0 :	\
			mm_abort_with_message(__LOCATION__, __FUNCTION__, "failed assertion: %s", #e))
# define DEBUG(...)	mm_debug(__LOCATION__, __FUNCTION__, __VA_ARGS__)
#else
# define ASSERT(e)	((void) 0)
# define DEBUG(...)	((void) 0)
#endif

void mm_abort_with_message(const char *restrict location,
			   const char *restrict function,
			   const char *restrict msg, ...)
	__attribute__((format(printf, 3, 4)))
	__attribute__((nonnull(1, 2, 3)))
	__attribute__((noreturn));

#if ENABLE_DEBUG
void mm_debug(const char *restrict location,
	      const char *restrict function,
	      const char *restrict msg, ...)
	__attribute__((format(printf, 3, 4)))
	__attribute__((nonnull(1, 2, 3)));
#else
# define mm_debug(...)	((void) 0)
#endif

#endif /* BASE_LOG_DEBUG_H */
