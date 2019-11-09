/*
 * base/timepiece.h - MainMemory internal clock.
 *
 * Copyright (C) 2019  Aleksey Demakov
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

#ifndef BASE_TIMEPIECE_H
#define BASE_TIMEPIECE_H

#include "common.h"
#include "base/arch/intrinsic.h"
#include "base/clock.h"
#include "base/report.h"

#ifndef ENABLE_TIMEPIECE_TIMESTAMP
# if MM_CPU_TIMESTAMP
#  define ENABLE_TIMEPIECE_TIMESTAMP	(1)
# endif
#endif

#define MM_TIMEPIECE_COUNT		(250)

/* Internal clock that is very coarse but takes very little CPU time on average.
   It is good enough for many tasks where time precision is not so essential. */
struct mm_timepiece
{
	/* The (almost) current monotonic time. */
	mm_timeval_t clock_value;

	/* The (almost) current real time. */
	mm_timeval_t real_clock_value;

#if ENABLE_TIMEPIECE_TIMESTAMP
	/* The timestamp difference required to refer the system clock. */
	uint32_t stamp_delta;
	/* CPU timestamps for the moments when the corresponding time was asked. */
	uint64_t clock_stamp;
	uint64_t real_clock_stamp;
#else
	/* Repeat count until the corresponding time has to be asked. */
	uint32_t clock_count;
	uint32_t real_clock_count;
#endif
};

void
mm_timepiece_init(void);

void NONNULL(1)
mm_timepiece_prepare(struct mm_timepiece *tp);

static inline void NONNULL(1)
mm_timepiece_reset(struct mm_timepiece *tp UNUSED)
{
#if ENABLE_TIMEPIECE_TIMESTAMP
	// Nothing to do.
#else
	tp->clock_count = 0;
	tp->real_clock_count = 0;
#endif
}

static inline mm_timeval_t NONNULL(1)
mm_timepiece_gettime(struct mm_timepiece *tp)
{
#if ENABLE_TIMEPIECE_TIMESTAMP
	uint64_t stamp = mm_cpu_tsc();
	if ((tp->clock_stamp + tp->stamp_delta) <= stamp)
	{
		tp->clock_stamp = stamp;
		tp->clock_value = mm_clock_gettime_monotonic_coarse();
		TRACE("%lld", (long long) tp->clock_value);
	}
#else
	if (tp->clock_count) {
		tp->clock_count--;
	} else {
		tp->clock_count = MM_TIMEPIECE_COUNT;
		tp->clock_value = mm_clock_gettime_monotonic_coarse();
		TRACE("%lld", (long long) tp->clock_value);
	}
#endif
	return tp->clock_value;
}

static inline mm_timeval_t NONNULL(1)
mm_timepiece_getrealtime(struct mm_timepiece *tp)
{
#if ENABLE_TIMEPIECE_TIMESTAMP
	uint64_t stamp = mm_cpu_tsc();
	if ((tp->real_clock_stamp + tp->stamp_delta) <= stamp)
	{
		tp->real_clock_stamp = stamp;
		tp->real_clock_value = mm_clock_gettime_realtime_coarse();
		TRACE("%lld", (long long) tp->real_clock_value);
	}
#else
	if (tp->real_clock_count) {
		tp->real_clock_count--;
	} else {
		tp->real_clock_count = MM_TIMEPIECE_COUNT;
		tp->real_clock_value = mm_clock_gettime_realtime_coarse();
		TRACE("%lld", (long long) tp->real_clock_value);
	}
#endif
	return tp->real_clock_value;
}

#endif /* BASE_TIMEPIECE_H */
