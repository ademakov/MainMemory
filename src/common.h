/*
 * common.h - MainMemory common definitions.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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
 * Basic architecture properties.
 **********************************************************************/

#if ARCH_X86
# include "base/arch/x86/basic.h"
#elif ARCH_X86_64
# include "base/arch/x86-64/basic.h"
#else
# include "base/arch/generic/basic.h"
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

/**********************************************************************
 * Macro Expansion Magic.
 **********************************************************************/

#define stringify_verbatim(x)	#x
#define stringify_expanded(x)	stringify_verbatim(x)

#define __LOCATION__		__FILE__ ":" stringify_expanded(__LINE__)

/**********************************************************************
 * Compiler Shortcuts.
 **********************************************************************/

#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

#define ALIGN(x)		__attribute__((__aligned__(x)))
#define CACHE_ALIGN		ALIGN(MM_CACHELINE)

#define NORETURN		__attribute__((__noreturn__))
#define NONNULL(...)		__attribute__((__nonnull__(__VA_ARGS__)))
#define FORMAT(...)		__attribute__((__format__(printf, __VA_ARGS__)))
#define UNUSED			__attribute__((__unused__))
#define MALLOC			__attribute__((__malloc__))

/**********************************************************************
 * Compiler Memory Ordering.
 **********************************************************************/

#define mm_compiler_barrier()	asm volatile("" ::: "memory")

#define mm_volatile_load(x)	(* (volatile typeof(x) *) &(x))

#define mm_volatile_store(x, v)	((* (volatile typeof(x) *) &(x)) = (v))

/**********************************************************************
 * CPU Cache Prefetch.
 **********************************************************************/

#define mm_prefetch(...)	__builtin_prefetch(__VA_ARGS__)

/**********************************************************************
 * Basic Definitions.
 **********************************************************************/

/* Sentinel time values. */
#define MM_TIMEVAL_MIN		INT64_MIN
#define MM_TIMEVAL_MAX		INT64_MAX

/* Infinite timeout. */
#define MM_TIMEOUT_INFINITE	((mm_timeout_t) -1)

/* A non-existent thread. */
#define MM_THREAD_NONE		((mm_thread_t) -1)

/* A non-existent core. */
#define MM_CORE_NONE		((mm_core_t) -1)
/* A pseudo-core corresponding to the current one. */
#define MM_CORE_SELF		((mm_core_t) -2)

/* A non-existent task. */
#define MM_TASK_NONE		((mm_task_t) -1)

/* Time value (in microseconds). */
typedef int64_t			mm_timeval_t;

/* Timeout (in microseconds). */
typedef uint32_t		mm_timeout_t;

/* Sequence number for data updates. */
typedef uint32_t		mm_stamp_t;

/* Thread ID. */
typedef uint16_t 		mm_thread_t;

/* Core ID. */
typedef uint16_t		mm_core_t;

/* Task ID. */
typedef uint32_t		mm_task_t;

/* Task execution result. */
typedef uintptr_t		mm_value_t;

/* Task execution routine. */
typedef mm_value_t (*mm_routine_t)(mm_value_t arg);

/*
 * Task and future special result codes.
 */

/* The result is unavailable as the task/future has been canceled. */
#define MM_RESULT_CANCELED	((mm_value_t) -1)

/* The result is unavailable as the task/future is still running. */
#define MM_RESULT_NOTREADY	((mm_value_t) -2)

/* The result is unavailable as the future has not yet started. */
#define MM_RESULT_DEFERRED	((mm_value_t) -3)

/* The result is unavailable as not needed in the first place. */
#define MM_RESULT_UNWANTED	((mm_value_t) -4)

#endif /* COMMON_H */
