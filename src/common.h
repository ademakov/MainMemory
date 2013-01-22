/*
 * common.h - MainMemory common definitions.
 *
 * Copyright (C) 2012-2013  Aleksey Demakov
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

#ifndef COMMON_H
#define COMMON_H

#ifndef __GNUC__
# error "Only GCC is currently supported."
#endif

/**********************************************************************
 * Common Headers.
 **********************************************************************/

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**********************************************************************
 * Compiler Helpers.
 **********************************************************************/

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/**********************************************************************
 * Common Macros.
 **********************************************************************/

#define min(a, b) ({			\
		typeof(a) _a = (a);	\
		typeof(b) _b = (b);	\
		_a < _b ? _a : _b;	\
	})

#define max(a, b) ({			\
		typeof(a) _a = (a);	\
		typeof(b) _b = (b);	\
		_a > _b ? _a : _b;	\
	})

#define containerof(field_ptr, type, field) \
	((type *) ((char *)(field_ptr) - offsetof(type, field)))

/**********************************************************************
 * Basic Definitions.
 **********************************************************************/

/* Task priorities. */
#define MM_PRIO_LOWEST		31
#define MM_PRIO_DEFAULT		15
#define MM_PRIO_HIGHEST		0

/* Task execution routine. */
typedef void (*mm_routine)(uintptr_t arg);

/* Infinite timeout. */
#define MM_TIMEOUT_INFINITE	((uint32_t) 0xFFFFFFFF)

/* Timeout (in microseconds). */
typedef uint32_t	mm_timeout_t;

#endif /* COMMON_H */
