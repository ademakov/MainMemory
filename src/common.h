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

#include "config.h"

/**********************************************************************
 * Common Standard Headers.
 **********************************************************************/

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
 * Compiler Shortcuts.
 **********************************************************************/

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define __align(x)	__attribute__((aligned (x)))

/**********************************************************************
 * Compiler Memory Ordering.
 **********************************************************************/

#define mm_compiler_barrier()	asm volatile("" ::: "memory")

#define mm_volatile_load(x)	(* (volatile typeof(x) *) &(x))

#define mm_volatile_store(x, v)	((* (volatile typeof(x) *) &(x)) = (v))

/**********************************************************************
 * Architecture Specific Definitions.
 **********************************************************************/

#include "arch/common.h"
#include "arch/memory.h"
#include "arch/atomic.h"
#include "arch/stack.h"

/**********************************************************************
 * Basic Definitions.
 **********************************************************************/

/* Sentinel time values. */
#define MM_TIMEVAL_MIN		INT64_MIN
#define MM_TIMEVAL_MAX		INT64_MAX

/* Infinite timeout. */
#define MM_TIMEOUT_INFINITE	((mm_timeout_t) 0xFFFFFFFF)

/* Time value (in microseconds). */
typedef int64_t			mm_timeval_t;

/* Timeout (in microseconds). */
typedef uint32_t		mm_timeout_t;

/* Task priorities. */
#define MM_PRIO_BOOT		31
#define MM_PRIO_IDLE		30
#define MM_PRIO_DEFAULT		15
#define MM_PRIO_HIGHEST		0

/* Task execution result. */
typedef uintptr_t mm_result_t;

/* Task execution routine. */
typedef mm_result_t (*mm_routine_t)(uintptr_t arg);

#endif /* COMMON_H */
