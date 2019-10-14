/*
 * base/event/timesource.h - MainMemory event time utilities.
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

#ifndef BASE_EVENT_TIMESOURCE_H
#define BASE_EVENT_TIMESOURCE_H

#include "common.h"
#include "base/arch/intrinsic.h"
#include "base/clock.h"
#include "base/report.h"

#ifndef ENABLE_TIMESOURCE_TIMESTAMP
# if MM_CPU_TIMESTAMP
#  define ENABLE_TIMESOURCE_TIMESTAMP	(1)
# endif
#endif

#define MM_EVENT_CLOCK_COUNT		(250)
#define MM_EVENT_CLOCK_STAMP_DELTA	(1000 * 1000)

/* Event time source is very coarse but it is good enough for many tasks where
   time precision is not so essential. */
struct mm_event_timesource
{
	/* The (almost) current monotonic time. */
	mm_timeval_t clock_value;

	/* The (almost) current real time. */
	mm_timeval_t real_clock_value;

#if ENABLE_TIMESOURCE_TIMESTAMP
	/* CPU timestamps for the moments when the corresponding time was asked. */
	uint64_t clock_stamp;
	uint64_t real_clock_stamp;
#else
	/* Repeat count until the corresponding time has to be asked. */
	uint32_t clock_count;
	uint32_t real_clock_count;
#endif
};

static inline void NONNULL(1)
mm_event_timesource_prepare(struct mm_event_timesource *ts)
{
#if ENABLE_TIMESOURCE_TIMESTAMP
	ts->clock_stamp = 0;
	ts->real_clock_stamp = 0;
#else
	ts->clock_count = 0;
	ts->real_clock_count = 0;
#endif
}

static inline void NONNULL(1)
mm_event_timesource_refresh(struct mm_event_timesource *ts UNUSED)
{
#if ENABLE_TIMESOURCE_TIMESTAMP
	// Nothing to do.
#else
	ts->clock_count = 0;
	ts->real_clock_count = 0;
#endif
}

static inline mm_timeval_t NONNULL(1)
mm_event_timesource_gettime(struct mm_event_timesource *ts)
{
#if ENABLE_TIMESOURCE_TIMESTAMP
	uint64_t stamp = mm_cpu_timestamp();
	if ((ts->clock_stamp + MM_EVENT_CLOCK_STAMP_DELTA) <= stamp)
	{
		ts->clock_stamp = stamp;
		ts->clock_value = mm_clock_gettime_monotonic_coarse();
		TRACE("%lld", (long long) ts->clock_value);
	}
#else
	if (ts->clock_count) {
		ts->clock_count--;
	} else {
		ts->clock_count = MM_EVENT_CLOCK_COUNT;
		ts->clock_value = mm_clock_gettime_monotonic_coarse();
		TRACE("%lld", (long long) ts->clock_value);
	}
#endif
	return ts->clock_value;
}

static inline mm_timeval_t NONNULL(1)
mm_event_timesource_getrealtime(struct mm_event_timesource *ts)
{
#if ENABLE_TIMESOURCE_TIMESTAMP
	uint64_t stamp = mm_cpu_timestamp();
	if ((ts->real_clock_stamp + MM_EVENT_CLOCK_STAMP_DELTA) <= stamp)
	{
		ts->real_clock_stamp = stamp;
		ts->real_clock_value = mm_clock_gettime_realtime_coarse();
		TRACE("%lld", (long long) ts->real_clock_value);
	}
#else
	if (ts->real_clock_count) {
		ts->real_clock_count--;
	} else {
		ts->real_clock_count = MM_EVENT_CLOCK_COUNT;
		ts->real_clock_value = mm_clock_gettime_realtime_coarse();
		TRACE("%lld", (long long) ts->real_clock_value);
	}
#endif
	return ts->real_clock_value;
}

#endif /* BASE_EVENT_TIMESOURCE_H */
