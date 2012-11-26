/*
 * common.h - MainMemory common definitions.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#ifndef COMMON_H
#define COMMON_H

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

#ifdef __GNUC__
# define likely(x)	__builtin_expect(!!(x), 1)
# define unlikely(x)	__builtin_expect(!!(x), 0)
#else
# define likely(x)	(x)
# define unlikely(x)	(x)
#endif

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

#endif /* COMMON_H */
