/*
 * base/clock.h - MainMemory time routines.
 *
 * Copyright (C) 2013,2019  Aleksey Demakov
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

#ifndef BASE_CLOCK_H
#define BASE_CLOCK_H

#include "common.h"

#ifdef HAVE_TIME_H
# include <time.h>
#endif

#if defined(CLOCK_REALTIME) && defined(CLOCK_MONOTONIC)
# define MM_CLOCK_REALTIME		((mm_clock_t) CLOCK_REALTIME)
# define MM_CLOCK_MONOTONIC		((mm_clock_t) CLOCK_MONOTONIC)
# if defined(CLOCK_REALTIME_COARSE) && defined(CLOCK_MONOTONIC_COARSE)
#  define MM_CLOCK_REALTIME_COARSE	((mm_clock_t) CLOCK_REALTIME_COARSE)
#  define MM_CLOCK_MONOTONIC_COARSE	((mm_clock_t) CLOCK_MONOTONIC_COARSE)
# elif defined(CLOCK_REALTIME_FAST) && defined(CLOCK_MONOTONIC_FAST)
#  define MM_CLOCK_REALTIME_COARSE	((mm_clock_t) CLOCK_REALTIME_FAST)
#  define MM_CLOCK_MONOTONIC_COARSE	((mm_clock_t) CLOCK_MONOTONIC_FAST)
# endif
#else
# define MM_CLOCK_REALTIME		((mm_clock_t) 0)
# define MM_CLOCK_MONOTONIC		((mm_clock_t) 1)
#endif

#ifndef ENABLE_COARSE_CLOCK
# ifdef MM_CLOCK_MONOTONIC_COARSE
#  define ENABLE_COARSE_CLOCK		1
# else
#  define ENABLE_COARSE_CLOCK		0
# endif
#endif

#ifndef MM_CLOCK_MONOTONIC_COARSE
# define MM_CLOCK_REALTIME_COARSE	MM_CLOCK_REALTIME
# define MM_CLOCK_MONOTONIC_COARSE	MM_CLOCK_MONOTONIC
#endif

typedef int mm_clock_t;

void
mm_clock_init(void);

mm_timeval_t
mm_clock_gettime_realtime(void);
mm_timeval_t
mm_clock_gettime_monotonic(void);

#if ENABLE_COARSE_CLOCK

mm_timeval_t
mm_clock_gettime_realtime_coarse(void);
mm_timeval_t
mm_clock_gettime_monotonic_coarse(void);

#else

#define mm_clock_gettime_realtime_coarse mm_clock_gettime_realtime
#define mm_clock_gettime_monotonic_coarse mm_clock_gettime_monotonic

#endif

mm_timeval_t
mm_clock_gettime(mm_clock_t clock);

#endif /* BASE_CLOCK_H */
